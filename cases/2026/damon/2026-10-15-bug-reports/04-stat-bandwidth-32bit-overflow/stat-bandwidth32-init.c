// SPDX-License-Identifier: GPL-2.0
/* Initramfs workload for observing DAMON_STAT bandwidth on i386. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/reboot.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define WORK_BYTES (32U * 1024U * 1024U)
#define PARAM "/sys/module/damon_stat/parameters/"

static double uptime_seconds(void)
{
	FILE *f = fopen("/proc/uptime", "r");
	double value = -1;

	if (!f)
		return -1;
	if (fscanf(f, "%lf", &value) != 1)
		value = -1;
	fclose(f);
	return value;
}

static void show(const char *name, const char *path)
{
	char buf[128] = {};
	int fd = open(path, O_RDONLY);
	ssize_t len;

	if (fd < 0) {
		printf("%s open_errno=%d\n", name, errno);
		return;
	}
	len = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (len <= 0) {
		printf("%s read_ret=%zd errno=%d\n", name, len, errno);
		return;
	}
	printf("%s %s", name, buf);
}

static void work(void)
{
	volatile unsigned char *buf = mmap(NULL, WORK_BYTES,
			PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
			-1, 0);
	unsigned long sweeps = 0;

	if (buf == MAP_FAILED) {
		printf("WORK_MMAP errno=%d\n", errno);
		_exit(2);
	}
	for (size_t i = 0; i < WORK_BYTES; i += 4096)
		buf[i] = 1;
	printf("WORK_STARTED bytes=%u pages=%u uptime=%.2f\n",
	       WORK_BYTES, WORK_BYTES / 4096, uptime_seconds());
	for (;;) {
		for (size_t i = 0; i < WORK_BYTES; i += 4096)
			buf[i]++;
		if (++sweeps % 100000 == 0)
			printf("WORK_SWEEPS %lu uptime=%.2f\n",
			       sweeps, uptime_seconds());
	}
}

int main(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };
	int fd, status;
	pid_t child;
	double start;

	mkdir("/dev", 0755);
	mknod("/dev/console", S_IFCHR | 0600, makedev(5, 1));
	fd = open("/dev/console", O_RDWR);
	if (fd >= 0) {
		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		if (fd > 2)
			close(fd);
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	mkdir("/sys", 0755);
	mkdir("/proc", 0755);
	if (mount("sysfs", "/sys", "sysfs", 0, NULL) ||
	    mount("proc", "/proc", "proc", 0, NULL)) {
		printf("MOUNT_FAILED errno=%d\n", errno);
		return 1;
	}
	printf("BANDWIDTH32_GUEST_READY long_bits=%zu uptime=%.2f\n",
	       sizeof(long) * 8, uptime_seconds());
	show("ENABLED", PARAM "enabled");
	show("KDAMOND_PID", PARAM "kdamond_pid");
	child = fork();
	if (child < 0) {
		printf("FORK_FAILED errno=%d\n", errno);
		return 2;
	}
	if (!child)
		work();
	start = uptime_seconds();
	while (uptime_seconds() < start + 23.0)
		nanosleep(&ts, NULL);
	printf("WORK_STOP uptime=%.2f\n", uptime_seconds());
	kill(child, SIGTERM);
	waitpid(child, &status, 0);
	show("AGGR_INTERVAL", PARAM "aggr_interval_us");
	show("ESTIMATED_BANDWIDTH", PARAM "estimated_memory_bandwidth");
	printf("BANDWIDTH32_GUEST_DONE\n");
	sync();
	syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		LINUX_REBOOT_CMD_POWER_OFF, NULL);
	for (;;)
		pause();
}
