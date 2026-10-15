// SPDX-License-Identifier: GPL-2.0
/* QEMU i386 guest: exercise DAMON VADDR intervals-goal arithmetic in kdamond. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/reboot.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define K "/sys/kernel/mm/damon/admin/kdamonds/0"
#define C K "/contexts/0"
#define I C "/monitoring_attrs/intervals"
#define G I "/intervals_goal"
#define T "/sys/kernel/tracing"
#define SZ (1024UL * 1024UL)

static int put(const char *path, const char *value)
{
	int fd = open(path, O_WRONLY);
	ssize_t n = -1;
	if (fd >= 0) {
		n = write(fd, value, strlen(value));
		close(fd);
	}
	printf("SET %s=%s ret=%zd errno=%d\n", path, value, n,
	       n < 0 ? errno : 0);
	return n == (ssize_t)strlen(value) ? 0 : -1;
}

static void show(const char *path)
{
	char buf[256] = {};
	int fd = open(path, O_RDONLY);
	ssize_t n = fd < 0 ? -1 : read(fd, buf, sizeof(buf) - 1);
	if (fd >= 0)
		close(fd);
	if (n > 0 && buf[n - 1] == '\n')
		buf[n - 1] = 0;
	printf("GET %s=%s ret=%zd errno=%d\n", path, buf, n,
	       n < 0 ? errno : 0);
}

static void sleep_ms(long ms)
{
	struct timespec ts = { .tv_sec = ms / 1000,
		.tv_nsec = (ms % 1000) * 1000000L };
	while (nanosleep(&ts, &ts) && errno == EINTR)
		;
}

static void child(int pipefd)
{
	volatile unsigned char *map = mmap(NULL, SZ, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	unsigned long addr = (unsigned long)map;
	volatile unsigned long sink = 0;
	if (map == MAP_FAILED)
		_exit(80);
	for (size_t i = 0; i < SZ; i += 4096)
		map[i] = 1;
	if (write(pipefd, &addr, sizeof(addr)) != sizeof(addr))
		_exit(81);
	close(pipefd);
	for (;;) {
		for (size_t i = 0; i < SZ; i += 4096)
			sink += map[i];
		sleep_ms(75);
	}
}

static int setup(unsigned long addr, pid_t pid)
{
	char path[512], val[64];
	const struct {
		const char *path, *value;
	} settings[] = {
		{ "/sys/kernel/mm/damon/admin/kdamonds/nr_kdamonds", "1" },
		{ K "/contexts/nr_contexts", "1" },
		{ C "/operations", "vaddr" },
		{ C "/targets/nr_targets", "1" },
		{ C "/targets/0/regions/nr_regions", "4" },
		{ I "/sample_us", "5000" },
		{ I "/aggr_us", "100000" },
		{ I "/update_us", "1000000" },
		{ G "/access_bp", "400" },
		{ G "/aggrs", "1" },
		{ G "/min_sample_us", "2500" },
		{ G "/max_sample_us", "10000" },
		{ C "/monitoring_attrs/nr_regions/min", "3" },
		{ C "/monitoring_attrs/nr_regions/max", "4" },
		{ C "/schemes/nr_schemes", "1" },
		{ C "/schemes/0/action", "stat" },
		{ C "/schemes/0/access_pattern/sz/max", "4294967295" },
		{ C "/schemes/0/access_pattern/nr_accesses/max", "4294967295" },
		{ C "/schemes/0/access_pattern/age/max", "4294967295" },
	};
	for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); i++)
		if (put(settings[i].path, settings[i].value))
			return -1;
	snprintf(val, sizeof(val), "%d", pid);
	if (put(C "/targets/0/pid_target", val))
		return -1;
	for (unsigned int i = 0; i < 4; i++) {
		snprintf(path, sizeof(path), C "/targets/0/regions/%u/start", i);
		snprintf(val, sizeof(val), "%lu", addr + i * SZ / 4);
		if (put(path, val))
			return -1;
		snprintf(path, sizeof(path), C "/targets/0/regions/%u/end", i);
		snprintf(val, sizeof(val), "%lu", addr + (i + 1) * SZ / 4);
		if (put(path, val))
			return -1;
	}
	return 0;
}

static void dump_trace(void)
{
	char buf[4096];
	int fd = open(T "/trace", O_RDONLY);
	ssize_t n;
	printf("TRACE_BEGIN errno=%d\n", fd < 0 ? errno : 0);
	if (fd < 0)
		return;
	while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
		buf[n] = 0;
		fwrite(buf, 1, n, stdout);
	}
	printf("TRACE_END read_ret=%zd errno=%d\n", n, n < 0 ? errno : 0);
	close(fd);
}

int main(void)
{
	int fds[2], fd;
	pid_t pid;
	unsigned long addr = 0;
	setvbuf(stdout, NULL, _IONBF, 0);
	mkdir("/dev", 0755);
	mknod("/dev/console", S_IFCHR | 0600, makedev(5, 1));
	fd = open("/dev/console", O_RDWR);
	if (fd >= 0) {
		dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
		if (fd > 2) close(fd);
	}
	mkdir("/sys", 0755);
	mkdir("/proc", 0755);
	if (mount("sysfs", "/sys", "sysfs", 0, NULL) ||
	    mount("proc", "/proc", "proc", 0, NULL)) {
		printf("MOUNT_FAILED errno=%d\n", errno);
		return 1;
	}
	mkdir(T, 0755);
	printf("TRACE_MOUNT ret=%d errno=%d\n",
	       mount("tracefs", T, "tracefs", 0, NULL), errno);
	show(C "/avail_operations");
	if (pipe(fds)) return 2;
	pid = fork();
	if (pid < 0) return 3;
	if (pid == 0) { close(fds[0]); child(fds[1]); }
	close(fds[1]);
	if (read(fds[0], &addr, sizeof(addr)) != sizeof(addr)) return 4;
	close(fds[0]);
	printf("TARGET pid=%d addr=%lu end=%lu bytes=%lu long_bits=%zu\n",
	       pid, addr, addr + SZ, SZ, sizeof(long) * 8);
	if (setup(addr, pid)) return 5;
	if (put(T "/events/damon/damos_before_apply/enable", "1") ||
	    put(T "/events/damon/damon_monitor_intervals_tune/enable", "1"))
		return 6;
	if (put(K "/state", "on")) return 7;
	for (int i = 0; i < 8; i++) {
		sleep_ms(250);
		if (put(K "/state", "update_tuned_intervals")) return 8;
		printf("OBS interval=%d\n", i);
		show(I "/sample_us");
		show(I "/aggr_us");
	}
	show(K "/pid");
	if (put(K "/state", "off")) return 9;
	dump_trace();
	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
	printf("INTERVALS32_GUEST_DONE\n");
	syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		LINUX_REBOOT_CMD_POWER_OFF, NULL);
	for (;;) pause();
}
