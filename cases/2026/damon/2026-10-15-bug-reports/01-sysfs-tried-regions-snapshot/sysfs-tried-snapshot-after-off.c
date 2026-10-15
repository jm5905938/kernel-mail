// SPDX-License-Identifier: GPL-2.0
/* Minimal initramfs guest reproducer for failed DAMON sysfs snapshot update. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/reboot.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define ROOT "/sys/kernel/mm/damon/admin/kdamonds"
#define K0 ROOT "/0"
#define CTX K0 "/contexts/0"
#define TRIED CTX "/schemes/0/tried_regions"

static void say(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}

static int put(const char *path, const char *value)
{
	int fd = open(path, O_WRONLY);
	ssize_t n;

	if (fd < 0) {
		say("WRITE_OPEN path=%s errno=%d\n", path, errno);
		return -1;
	}
	n = write(fd, value, strlen(value));
	if (n != (ssize_t)strlen(value)) {
		say("WRITE path=%s value=%s result=%zd errno=%d\n",
		    path, value, n, errno);
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static long number(const char *path)
{
	char buf[128] = {};
	int fd = open(path, O_RDONLY);
	ssize_t n;

	if (fd < 0) {
		say("READ_OPEN path=%s errno=%d\n", path, errno);
		return -1;
	}
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0) {
		say("READ path=%s result=%zd errno=%d\n", path, n, errno);
		return -1;
	}
	return strtol(buf, NULL, 10);
}

static int region_count(void)
{
	DIR *dir = opendir(TRIED);
	struct dirent *de;
	int count = 0;

	if (!dir)
		return -1;
	while ((de = readdir(dir)))
		if (de->d_name[0] >= '0' && de->d_name[0] <= '9')
			count++;
	closedir(dir);
	return count;
}

static void finish(int code)
{
	say("RESULT exit=%d\n", code);
	sync();
	syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		LINUX_REBOOT_CMD_RESTART, NULL);
	for (;;)
		pause();
}

int main(void)
{
	char path[512];
	int fd, saved_errno;
	long before, after;
	int before_dirs, after_dirs;
	struct {
		const char *path;
		const char *value;
	} settings[] = {
		{ ROOT "/nr_kdamonds", "1" },
		{ K0 "/contexts/nr_contexts", "1" },
		{ CTX "/operations", "paddr" },
		{ CTX "/targets/nr_targets", "1" },
		{ CTX "/targets/0/regions/nr_regions", "1" },
		{ CTX "/targets/0/regions/0/start", "4194304" },
		{ CTX "/targets/0/regions/0/end", "8388608" },
		{ CTX "/schemes/nr_schemes", "1" },
		{ CTX "/schemes/0/access_pattern/sz/max", "4294967295" },
		{ CTX "/schemes/0/access_pattern/nr_accesses/max", "4294967295" },
		{ CTX "/schemes/0/access_pattern/age/max", "4294967295" },
	};

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
	mkdir("/sys", 0755);
	if (mount("sysfs", "/sys", "sysfs", 0, NULL)) {
		say("MOUNT_SYSFS errno=%d\n", errno);
		finish(1);
	}
	say("GUEST_READY\n");
	for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); i++)
		if (put(settings[i].path, settings[i].value))
			finish(2);
	if (put(K0 "/state", "on"))
		finish(3);
	say("STATE_ON\n");
	if (put(K0 "/state", "update_schemes_tried_regions"))
		finish(4);
	snprintf(path, sizeof(path), "%s/total_bytes", TRIED);
	before = number(path);
	before_dirs = region_count();
	say("BEFORE_OFF bytes=%ld dirs=%d\n", before, before_dirs);
	if (put(K0 "/state", "off"))
		finish(5);
	say("STATE_OFF bytes=%ld dirs=%d\n", number(path), region_count());
	fd = open(K0 "/state", O_WRONLY);
	if (fd < 0)
		finish(6);
	errno = 0;
	ssize_t ret = write(fd, "update_schemes_tried_regions", 28);
	saved_errno = errno;
	close(fd);
	after = number(path);
	after_dirs = region_count();
	say("FAILED_UPDATE ret=%zd errno=%d bytes_after=%ld dirs_after=%d\n",
	    ret, saved_errno, after, after_dirs);
	finish(before > 0 && before_dirs > 0 && ret == -1 &&
	       saved_errno == ECANCELED && after == 0 && after_dirs == 0 ? 0 : 7);
}
