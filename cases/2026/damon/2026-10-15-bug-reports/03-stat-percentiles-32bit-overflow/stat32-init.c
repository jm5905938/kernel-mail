// SPDX-License-Identifier: GPL-2.0
/* Minimal 32-bit guest for DAMON_STAT percentile validation. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/reboot.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#define PARAM "/sys/module/damon_stat/parameters/"

static void show(const char *label, const char *path)
{
	char buf[8192] = {};
	int fd = open(path, O_RDONLY);
	ssize_t len;

	if (fd < 0) {
		printf("%s open_errno=%d\n", label, errno);
		return;
	}
	len = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (len <= 0) {
		printf("%s read_ret=%zd errno=%d\n", label, len, errno);
		return;
	}
	buf[len] = 0;
	printf("%s %s\n", label, buf);
}

static void percentiles(void)
{
	char buf[8192] = {};
	char *cursor = buf, *end;
	long first = 0, middle = 0, last = 0;
	int fd = open(PARAM "memory_idle_ms_percentiles", O_RDONLY);
	ssize_t len;

	if (fd < 0) {
		printf("PERCENTILES_OPEN errno=%d\n", errno);
		return;
	}
	len = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (len <= 0) {
		printf("PERCENTILES_READ ret=%zd errno=%d\n", len, errno);
		return;
	}
	buf[len] = 0;
	for (int i = 0; i <= 100; i++) {
		long value = strtol(cursor, &end, 10);
		if (end == cursor) {
			printf("PERCENTILES_PARSE index=%d failed\n", i);
			return;
		}
		if (i == 0)
			first = value;
		if (i == 50)
			middle = value;
		if (i == 100)
			last = value;
		cursor = end;
		if (i < 100 && *cursor == ',')
			cursor++;
	}
	printf("PERCENTILES p0=%ld p50=%ld p100=%ld bytes=%zd\n",
	       first, middle, last, len);
}

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

static int wait_guest_seconds(double seconds)
{
	double start = uptime_seconds();
	unsigned long waits = 0;
	struct timespec ts = { 0, 100000000 };

	printf("WAIT_GUEST start=%.2f seconds=%.0f\n", start, seconds);
	while (start >= 0 && uptime_seconds() < start + seconds) {
		int ret;

		errno = 0;
		ret = nanosleep(&ts, NULL);
		if (ret < 0 && errno != EINTR) {
			printf("WAIT_GUEST_ERROR ret=%d errno=%d\n", ret, errno);
			return -1;
		}
		if (++waits > 1000) {
			printf("WAIT_GUEST_STUCK uptime=%.2f\n", uptime_seconds());
			return -1;
		}
	}
	printf("WAIT_GUEST_DONE uptime=%.2f waits=%lu\n",
	       uptime_seconds(), waits);
	return 0;
}

int main(void)
{
	int fd;

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
	printf("STAT32_GUEST_READY long_bits=%zu\n", sizeof(long) * 8);
	printf("MOUNT_SYSFS ret=%d errno=%d\n",
	       mount("sysfs", "/sys", "sysfs", 0, NULL), errno);
	printf("MOUNT_PROC ret=%d errno=%d\n",
	       mount("proc", "/proc", "proc", 0, NULL), errno);
	show("MEMINFO", "/proc/meminfo");
	show("UPTIME_START", "/proc/uptime");
	show("ENABLED", PARAM "enabled");
	show("KDAMOND_PID", PARAM "kdamond_pid");
	show("AGGR_INTERVAL", PARAM "aggr_interval_us");
	show("KDAMOND_STATUS_START", "/proc/21/status");
	show("KDAMOND_WCHAN_START", "/proc/21/wchan");
	{
		unsigned int remain;
		struct { int sec; long nsec; } ts = { 1, 0 };
		long ret;
		errno = 0;
		remain = sleep(1);
		printf("SLEEP_PROBE remain=%u errno=%d uptime=%.2f\n",
		       remain, errno, uptime_seconds());
		errno = 0;
		ret = syscall(SYS_nanosleep, &ts, NULL);
		printf("RAW_NANOSLEEP ret=%ld errno=%d uptime=%.2f\n",
		       ret, errno, uptime_seconds());
	}
	wait_guest_seconds(8);
	show("UPTIME_AFTER_8S", "/proc/uptime");
	show("ENABLED_AFTER", PARAM "enabled");
	show("KDAMOND_PID_AFTER", PARAM "kdamond_pid");
	show("AGGR_INTERVAL_AFTER", PARAM "aggr_interval_us");
	show("KDAMOND_STATUS_AFTER_8S", "/proc/21/status");
	show("KDAMOND_WCHAN_AFTER_8S", "/proc/21/wchan");
	wait_guest_seconds(12);
	show("UPTIME_AFTER_20S", "/proc/uptime");
	show("AGGR_INTERVAL_AFTER_20S", PARAM "aggr_interval_us");
	show("KDAMOND_STATUS_AFTER_20S", "/proc/21/status");
	show("KDAMOND_WCHAN_AFTER_20S", "/proc/21/wchan");
	show("ESTIMATED_BANDWIDTH", PARAM "estimated_memory_bandwidth");
	percentiles();
	printf("STAT32_GUEST_DONE\n");
	sync();
	syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		LINUX_REBOOT_CMD_POWER_OFF, NULL);
	for (;;)
		pause();
}
