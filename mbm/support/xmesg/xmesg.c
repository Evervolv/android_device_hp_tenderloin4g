/* vi: set sw=4 ts=4: */
/*
 * dmesg - display kernel ring buffer with wall-clock timestamps.
 *    modeled after "logcat -v time" format.
 *
 * Copyright 2011 Howard Chu <hyc@xdandroid.com>
 *
 * Licensed under GPLv2.
 */

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <syslog.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/klog.h>

const char *levels[] = {
	"EMERG",
	"ALERT",
	"CRIT ",
	"ERROR",
	"WARN ",
	"NOTE ",
	"INFO ",
	"DEBUG"
};

void die(const char *msg)
{
	perror(msg);
	exit(1);
}

/* If we find no timestamps to reference in the existing buffer,
 * write a timestamp into the log so that we can correlate it to
 * the kernel timestamps.
 */
void log_stamp()
{
	int fd;
	struct timeval tv;
	struct tm *tm;
	char buf[sizeof("<X>(2011-06-01 00:00:00.123456789 UTC)\n")];

	/* On vanilla Linux /dev/kmsg always exists. On Android
	 * for some reason it's been deleted, so we create the
	 * device node on the fly and use it as needed. This of
	 * course only works when running as root.
	 */
	mknod("/dev/__kmsg__", S_IFCHR | 0600, (1 << 8) | 11);
	fd = open("/dev/__kmsg__", O_WRONLY);

	/* Write a timestamp in the same format used by kernel DEBUG_SUSPEND */
	gettimeofday(&tv, NULL);
	tm = gmtime(&tv.tv_sec);
	sprintf(buf, "<%d>(%d-%02d-%02d %02d:%02d:%02d.%06u000 UTC)\n",
		LOG_INFO,
		tm->tm_year + 1900, tm->tm_mon+1, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec, tv.tv_usec);
	write(fd, buf, sizeof(buf)-1);
	close(fd);
	unlink("/dev/__kmsg__");
}

/* Curse those POSIX idiots who thought it was a better idea to provide
 * localtime instead of GMT in mktime().
 */
time_t my_timegm(struct tm *tm)
{
	time_t ret;
	char *tz;

	tz = getenv("TZ");
	setenv("TZ", "", 1);
	tzset();
	ret = mktime(tm);
	if (tz)
		setenv("TZ", tz, 1);
	else
		unsetenv("TZ");
	tzset();
	return ret;
}

/* Parse a wallclock timestamp in kernel DEBUG_SUSPEND format 
 * and map it to its accompanying kernel timestamp, then save
 * the resulting offset. This offset will be used to map all
 * subsequent kernel timestamps to wallclock time.
 */
void get_offset(char *buf, char *ptr, struct timeval *offset)
{
	struct tm tm = {0};
	struct timeval stamp = {0,0};

	if (ptr[-3] == ':')
		ptr -= sizeof("2011-01-01 00:11:22")-1;
	else
		ptr -= sizeof("2011-01-01 00:11:22.330000000")-1;
	strptime(ptr, "%Y-%m-%d %T", &tm);
	sscanf(ptr + sizeof("2011-01-01 00:11:22"), "%06d", &stamp.tv_usec);
	stamp.tv_sec = my_timegm(&tm);

	while (ptr > buf) {
		if (*ptr == ']')
			break;
		ptr--;
	}
	if (ptr > buf) {
		while (ptr >= buf) {
			if (*ptr == '[')
				break;
			ptr--;
		}
		if (ptr < buf)
			return;
		sscanf(ptr+1, "%d.%d", &offset->tv_sec, &offset->tv_usec);
		offset->tv_usec = stamp.tv_usec - offset->tv_usec;
		offset->tv_sec = stamp.tv_sec - offset->tv_sec;
		if (offset->tv_usec < 0) {
			offset->tv_usec += 1000000;
			offset->tv_sec --;
		}
	}
}

int main(int argc, char **argv)
{
	int len, blen, level = LOG_INFO;
	char *buf, *ptr, *cur, *nl, *end, *utc, *utc2;
	struct timeval offset = {0,0};

	tzset();

#if 1
	blen = klogctl(10, NULL, 0); /* read ring buffer size */
#else
	blen = 262144;
#endif
	if (blen < 16*1024)
		blen = 16*1024;
	if (blen > 16*1024*1024)
		blen = 16*1024*1024;

	buf = malloc(blen+1);
	if (!buf)
		die("malloc failed");

#if 1
	len = klogctl(3, buf, blen); /* read ring buffer */
#else
	len = read(0, buf, blen);
#endif
	if (len < 0)
		die("klogctl");
	if (len == 0)			/* nothing to do */
		exit(0);

	end = buf + len;
	/* Find first timestamp */
	buf[len] = '\0';
	utc = strstr(buf, " UTC");
	if (!utc) {
		/* no wallclock time, insert our own */
		log_stamp();

		len = klogctl(3, buf, blen); /* read ring buffer again */
		if (len < 0)
			die("klogctl");
		buf[len] = '\0';
		utc = strstr(buf, " UTC");
	}
	if (utc) {
		get_offset(buf, utc, &offset);
		utc2 = strstr(utc+4, " UTC");
		if (!utc2)
			utc2 = end;
	}

	cur = buf;

	/* Log records are of the form <X>[yyyy.zzzzzz] text
	 * where X is the log priority, and yyyy.zzzzzz are the kernel's internal
	 * timestamp. (jiffies). If the circular buffer has wrapped around, the
	 * first record in the buffer may be missing its beginning.
	 */
	while(cur < end) {
		nl = strchr(cur, '\n');
		if (*cur == '<') {
			level = cur[1] - '0';
			cur += 3;
		}
		if (cur > utc) {
			if (nl > utc2) {
				utc = utc2;
				get_offset(cur, utc, &offset);
				utc2 = strstr(nl+1, " UTC");
				if (!utc2)
					utc2 = end;
			}
		}
		if (nl)
			*nl = '\0';
		if (*cur == '[') {
			struct timeval stamp = {0,0};
			struct tm *tm;
			sscanf(cur+1, "%d.%d", &stamp.tv_sec, &stamp.tv_usec);
			stamp.tv_sec += offset.tv_sec;
			stamp.tv_usec += offset.tv_usec;
			if (stamp.tv_usec >= 1000000) {
				stamp.tv_usec -= 1000000;
				stamp.tv_sec++;
			}
			tm = localtime(&stamp.tv_sec);
			printf("%02d-%02d %02d:%02d:%02d.%03d",
				tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
				(int)(stamp.tv_usec / 1000));
			ptr = strchr(cur, ']');
			if (ptr)
				cur = ptr+1;
		} else {
			printf("                  ");
		}
		printf(" %s:%s\n", levels[level], cur);
		cur = nl+1;
	}

	return 0;
}
