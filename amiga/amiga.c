/*
 * AmigaOS 3.x (m68k, ixemul) support for rsync.  See amiga.h for the idea.
 *
 * Copyright (C) 2026 Oliver Urbann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#define AMIGA_NO_WRAP
#include "rsync.h"
#include "ifuncs.h"

#ifdef __amigaos__

#include <sys/wait.h>

extern char **environ;

/* ixemul's startup gives main() a stack of this size, also in the
 * processes started via vfork()+exec.  The 4 KB a Shell gives by default
 * is far too little for rsync. */
unsigned long __stack = 256 * 1024;

#define AMIGA_MAXFD 256
#define REPLAY_ENV "RSYNC_AMIGA_REPLAY"

enum { M_NONE, M_RECORD, M_REPLAY };

static int mode = M_NONE;
static char prefix[MAXPATHLEN];	/* T:rsync-<pid>-<n>, record files get .<fd> / .det */
static char saved_cwd[MAXPATHLEN];
static char self_exe[MAXPATHLEN];
static char **saved_argv;

static int rec_fd[AMIGA_MAXFD];	/* M_RECORD: record file per stream fd */
static int rep_fd[AMIGA_MAXFD];	/* M_REPLAY: open record file per stream fd */
static char rep_done[AMIGA_MAXFD]; /* M_REPLAY: record for this fd used up */
static off_t rep_left[AMIGA_MAXFD];
static int det_fd = -1;
static int record_failed;
static int suppress;	/* M_REPLAY before the fork point: stay silent */

static int inherited_err = -1, inherited_sock = -1;
static int wrapup_fd = -1;	/* receiver: read end of the generator's wrap-up pipe */
static int wrapup_out = -1;	/* generator: write end */
static int noted_sock = -1;

#include <stdarg.h>

/* Set RSYNC_AMIGA_TRACE=<file> to log what the fork emulation does. */
static void trace(const char *fmt, ...)
{
	static const char *file;
	static int checked;
	char buf[512];
	va_list ap;
	int fd, n;

	if (!checked) {
		file = getenv("RSYNC_AMIGA_TRACE");
		checked = 1;
	}
	if (!file || !*file)
		return;
	n = snprintf(buf, sizeof buf, "[%ld] ", (long)getpid());
	va_start(ap, fmt);
	n += vsnprintf(buf + n, sizeof buf - n - 1, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof buf - 2)
		n = sizeof buf - 2;
	buf[n++] = '\n';
	if ((fd = open(file, O_WRONLY|O_CREAT|O_APPEND, 0644)) >= 0) {
		write(fd, buf, n);
		close(fd);
	}
}

/* With tracing on, log where a crash happened: pc minus the address of
 * amiga_init gives an offset to look up in the linker map. */
static void crash_handler(int sig, int code, struct sigcontext *scp)
{
	trace("signal %d at pc %#x (amiga_init at %#x, offset %#x)", sig,
	      scp ? (unsigned)scp->sc_pc : 0, (unsigned)(long)amiga_init,
	      scp ? (unsigned)(scp->sc_pc - (long)amiga_init) : 0);
	(void)code;
	_exit(128 + sig);
}

void amiga_trace_msg(int code, const char *buf, int len)
{
	while (len > 0 && buf[len-1] == '\n')
		len--;
	trace("msg %d%s: %.*s", code, suppress ? " (replay)" : "", len, buf);
}

static void die(const char *msg)
{
	trace("fatal: %s", msg);
	fprintf(stderr, "rsync (Amiga): %s\n", msg);
	fflush(stderr);
	_exit(RERR_IPC);
}

static void rec_name(char *buf, size_t sz, const char *suffix)
{
	snprintf(buf, sz, "%s.%s", prefix, suffix);
}

static int is_stream(int fd)
{
	struct stat st;
	if (fstat(fd, &st) < 0)
		return 0;
	return !S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode);
}

static void find_self(const char *argv0)
{
	const char *env = getenv("RSYNC_EXE");
	const char *base;
	char dir[MAXPATHLEN];

	if (env && *env) {
		strlcpy(self_exe, env, sizeof self_exe);
		return;
	}
	if ((base = strrchr(argv0, '/')) != NULL || (base = strrchr(argv0, ':')) != NULL)
		base++;
	else
		base = argv0;

	/* A vfork()ed child has no PROGDIR:, so turn it into a real path now. */
	if (chdir("PROGDIR:") == 0 && getcwd(dir, sizeof dir)) {
		snprintf(self_exe, sizeof self_exe, "%s%s%s", dir,
			 dir[strlen(dir)-1] == '/' || dir[strlen(dir)-1] == ':' ? "" : "/", base);
	} else
		strlcpy(self_exe, argv0, sizeof self_exe);
	if (*saved_cwd && chdir(saved_cwd) < 0)
		die("cannot return to the current directory");
}

const char *amiga_self_exe(void)
{
	return self_exe;
}

/*
 * File times and time zones.
 *
 * AmigaDOS stores file dates as local wall-clock time.  ixemul turns them
 * into Unix time with one fixed offset (ENV:IXGMTOFFSET, normally unset,
 * i.e. 0), so without help a file written at 13:00 CET reaches a Linux
 * server as 13:00 UTC, and summer time is never taken into account.
 *
 * When TZ is set (e.g. "CET-1CEST,M3.5.0,M10.5.0/3"), we undo ixemul's
 * offset to get the wall-clock time back and convert it with the TZ rules
 * that apply on that file's own date.  That is right all year round and
 * does not change when the clock goes to or from summer time.  Without TZ
 * nothing changes, so existing backups stay as they are.
 */
static int tz_on = -1;
static long ix_gmtoff;

static int tz_active(void)
{
	if (tz_on < 0) {
		const char *tz = getenv("TZ");
		tz_on = tz && *tz;
		if (tz_on) {
			tzset();
			ix_gmtoff = amiga_ix_gmt_offset();
		}
	}
	return tz_on;
}

/* timegm(): broken-down UTC to seconds since 1970. */
static time_t civil_to_time(const struct tm *tm)
{
	long y = tm->tm_year + 1900, m = tm->tm_mon + 1, d = tm->tm_mday;
	long era, yoe, doy, doe;

	if (m <= 2)
		y--;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = y - era * 400;
	doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return (time_t)(era * 146097 + doe - 719468) * 86400
	     + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
}

/* Time from ixemul's stat() to real Unix time. */
time_t amiga_time_from_fs(time_t t)
{
	struct tm tm, *g;
	time_t wall, u;

	if (!tz_active())
		return t;
	wall = t - ix_gmtoff;
	if (!(g = gmtime(&wall)))
		return t;
	tm = *g;
	tm.tm_isdst = -1;
	u = mktime(&tm);
	return u == (time_t)-1 ? t : u;
}

time_t amiga_time(time_t *t)
{
	time_t now = amiga_time_from_fs(time(NULL));
	if (t)
		*t = now;
	return now;
}

/* Real Unix time to the value ixemul's utimes() expects. */
time_t amiga_time_to_fs(time_t u)
{
	struct tm *l;

	if (!tz_active())
		return u;
	if (!(l = localtime(&u)))
		return u;
	return civil_to_time(l) + ix_gmtoff;
}

void amiga_init(int argc, char *argv[])
{
	static int seq;
	const char *env, *tmp;
	int i;

	for (i = 0; i < AMIGA_MAXFD; i++)
		rec_fd[i] = rep_fd[i] = -1;

	if (getenv("RSYNC_AMIGA_TRACE")) {
		(signal)(SIGBUS, (void (*)())crash_handler);
		(signal)(SIGSEGV, (void (*)())crash_handler);
		(signal)(SIGILL, (void (*)())crash_handler);
	}

	saved_argv = malloc((argc + 1) * sizeof (char *));
	if (!saved_argv)
		die("out of memory");
	for (i = 0; i < argc; i++)
		saved_argv[i] = strdup(argv[i]);
	saved_argv[argc] = NULL;

	if ((env = getenv(REPLAY_ENV)) != NULL) {
		/* We are the receiver half of a parent that reached its fork point:
		 * <prefix>|<cwd>|<error fd>|<socket fd>|<wrap-up fd> */
		char *copy = strdup(env), *p, *f[5];
		int n = 0;
		for (p = copy; n < 5; n++) {
			f[n] = p;
			if (!(p = strchr(p, '|')))
				break;
			*p++ = '\0';
		}
		if (n != 4)
			die("bad " REPLAY_ENV);
		strlcpy(prefix, f[0], sizeof prefix);
		strlcpy(saved_cwd, f[1], sizeof saved_cwd);
		inherited_err = atoi(f[2]);
		inherited_sock = atoi(f[3]);
		wrapup_fd = atoi(f[4]);
		free(copy);
		unsetenv(REPLAY_ENV);
		if (chdir(saved_cwd) < 0)
			die("cannot change to the parent's directory");
		find_self(argv[0]);
		mode = M_REPLAY;
		suppress = 1;
		trace("replay start: prefix=%s cwd=%s err=%d sock=%d self=%s",
		      prefix, saved_cwd, inherited_err, inherited_sock, self_exe);
		return;
	}

	if (!getcwd(saved_cwd, sizeof saved_cwd) || !*saved_cwd) {
		if (!amiga_cwd_name(saved_cwd, sizeof saved_cwd))
			*saved_cwd = '\0';
	}
	find_self(argv[0]);

	if (!(tmp = getenv("RSYNC_AMIGA_TMP")))
		tmp = "T:";
	snprintf(prefix, sizeof prefix, "%s%srsync-%ld-%d", tmp,
		 *tmp && strchr(":/", tmp[strlen(tmp)-1]) ? "" : "/", (long)getpid(), seq++);
	mode = M_RECORD;
	trace("start: argv0=%s self=%s cwd=%s prefix=%s", argv[0], self_exe, saved_cwd, prefix);
}

static void close_records(int remove)
{
	char name[MAXPATHLEN], num[16];
	int i;

	for (i = 0; i < AMIGA_MAXFD; i++) {
		if (rec_fd[i] < 0)
			continue;
		close(rec_fd[i]);
		rec_fd[i] = -1;
		if (remove) {
			snprintf(num, sizeof num, "%d", i);
			rec_name(name, sizeof name, num);
			unlink(name);
		}
	}
	if (det_fd >= 0) {
		close(det_fd);
		det_fd = -1;
		if (remove) {
			rec_name(name, sizeof name, "det");
			unlink(name);
		}
	}
}

void amiga_stop_recording(void)
{
	if (mode == M_RECORD) {
		trace("stop recording (sender)");
		close_records(1);
		mode = M_NONE;
	}
}

void amiga_cleanup(void)
{
	char name[MAXPATHLEN], num[16];
	int i;

	if (getenv("RSYNC_AMIGA_KEEP")) /* keep records for debugging */
		return;

	if (mode == M_RECORD)
		close_records(1);
	else if (mode == M_REPLAY) {
		for (i = 0; i < AMIGA_MAXFD; i++) {
			if (rep_fd[i] < 0)
				continue;
			close(rep_fd[i]);
			rep_fd[i] = -1;
			snprintf(num, sizeof num, "%d", i);
			rec_name(name, sizeof name, num);
			unlink(name);
		}
	}
}

int amiga_replaying(void)
{
	return suppress;
}

static void record(int fd, const void *buf, ssize_t n)
{
	char name[MAXPATHLEN], num[16];

	if (fd < 0 || fd >= AMIGA_MAXFD) {
		record_failed = 1;
		return;
	}
	if (rec_fd[fd] < 0) {
		snprintf(num, sizeof num, "%d", fd);
		rec_name(name, sizeof name, num);
		if ((rec_fd[fd] = open(name, O_WRONLY|O_CREAT|O_TRUNC, 0600)) < 0) {
			trace("cannot create record %s: %s", name, strerror(errno));
			record_failed = 1;
			return;
		}
		trace("recording fd %d to %s", fd, name);
	}
	if (write(rec_fd[fd], buf, n) != n)
		record_failed = 1;
}

/* Open the record of fd for replay; returns 0 if there is none (any more). */
static int replay_open(int fd)
{
	char name[MAXPATHLEN], num[16];
	struct stat st;

	if (fd < 0 || fd >= AMIGA_MAXFD || rep_done[fd])
		return 0;
	if (rep_fd[fd] >= 0)
		return 1;
	snprintf(num, sizeof num, "%d", fd);
	rec_name(name, sizeof name, num);
	if ((rep_fd[fd] = open(name, O_RDONLY)) < 0 || fstat(rep_fd[fd], &st) < 0) {
		rep_done[fd] = 1;
		return 0;
	}
	rep_left[fd] = st.st_size;
	if (!rep_left[fd]) {
		close(rep_fd[fd]);
		rep_fd[fd] = -1;
		unlink(name);
		rep_done[fd] = 1;
		return 0;
	}
	return 1;
}

ssize_t amiga_read(int fd, void *buf, size_t len)
{
	ssize_t n;

	if (mode == M_REPLAY && replay_open(fd)) {
		if ((off_t)len > rep_left[fd])
			len = rep_left[fd];
		if ((n = read(rep_fd[fd], buf, len)) <= 0)
			die("record file unreadable");
		if (!(rep_left[fd] -= n)) {
			char name[MAXPATHLEN], num[16];
			close(rep_fd[fd]);
			rep_fd[fd] = -1;
			rep_done[fd] = 1;
			snprintf(num, sizeof num, "%d", fd);
			rec_name(name, sizeof name, num);
			unlink(name);
		}
		return n;
	}

	n = read(fd, buf, len);
	if (mode == M_RECORD && n > 0 && is_stream(fd))
		record(fd, buf, n);
	return n;
}

ssize_t amiga_write(int fd, const void *buf, size_t len)
{
	/* Before its fork point the child repeats what its parent already
	 * said on the connection, so it must stay silent there. */
	if (suppress && is_stream(fd))
		return len;
	return write(fd, buf, len);
}

void amiga_check_wrapup(void)
{
	extern volatile sig_atomic_t got_sigusr2;
	struct pollfd pfd;
	char c;

	if (wrapup_fd < 0 || suppress)
		return;
	pfd.fd = wrapup_fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	if (poll(&pfd, 1, 0) > 0 && pfd.revents) {
		if (read(wrapup_fd, &c, 1) <= 0 || c == 'W') {
			trace("wrap-up received");
			got_sigusr2 = 1;
		}
	}
}

/* Replaces kill(receiver, SIGUSR2) at the end of do_recv(). */
int amiga_tell_receiver_wrapup(pid_t pid)
{
	(void)pid;
	if (wrapup_out < 0)
		return -1;
	trace("sending wrap-up to receiver");
	return write(wrapup_out, "W", 1) == 1 ? 0 : -1;
}

int amiga_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	nfds_t i;
	int ready = 0;

	if (mode == M_REPLAY) {
		for (i = 0; i < nfds; i++) {
			fds[i].revents = 0;
			if (fds[i].fd < 0)
				continue;
			if (fds[i].events & POLLIN && replay_open(fds[i].fd))
				fds[i].revents |= POLLIN;
			if (fds[i].events & POLLOUT && suppress && is_stream(fds[i].fd))
				fds[i].revents |= POLLOUT;
			if (fds[i].revents)
				ready++;
		}
		if (ready)
			return ready;
	}
	if (wrapup_fd >= 0 && !suppress && nfds < 16) {
		/* The receiver also waits for the generator's wrap-up here. */
		struct pollfd all[17];
		int cnt;
		memcpy(all, fds, nfds * sizeof *fds);
		all[nfds].fd = wrapup_fd;
		all[nfds].events = POLLIN;
		all[nfds].revents = 0;
		cnt = poll(all, nfds + 1, timeout);
		memcpy(fds, all, nfds * sizeof *fds);
		if (cnt > 0 && all[nfds].revents) {
			amiga_check_wrapup();
			cnt--;
		}
		return cnt;
	}
	return poll(fds, nfds, timeout);
}

void amiga_note_fd(const char *what, int fd)
{
	if (strcmp(what, "sock") == 0)
		noted_sock = fd;
}

int amiga_inherited_fd(const char *what)
{
	if (mode != M_REPLAY)
		return -1;
	if (strcmp(what, "sock") == 0)
		return inherited_sock;
	return -1;
}

void amiga_det(const char *tag, void *val, size_t len)
{
	char name[MAXPATHLEN], t[32];
	unsigned char hdr[5];
	size_t tlen = strlen(tag);

	if (tlen >= sizeof t)
		tlen = sizeof t - 1;

	if (mode == M_RECORD) {
		if (det_fd < 0) {
			rec_name(name, sizeof name, "det");
			if ((det_fd = open(name, O_WRONLY|O_CREAT|O_TRUNC, 0600)) < 0) {
				record_failed = 1;
				return;
			}
		}
		hdr[0] = tlen;
		hdr[1] = len >> 24; hdr[2] = len >> 16; hdr[3] = len >> 8; hdr[4] = len;
		if (write(det_fd, hdr, 5) != 5 || write(det_fd, tag, tlen) != (ssize_t)tlen
		 || write(det_fd, val, len) != (ssize_t)len)
			record_failed = 1;
	} else if (suppress) {
		size_t got;
		if (det_fd < 0) {
			rec_name(name, sizeof name, "det");
			if ((det_fd = open(name, O_RDONLY)) < 0)
				die("missing recorded values");
		}
		if (read(det_fd, hdr, 5) != 5 || hdr[0] != tlen
		 || read(det_fd, t, tlen) != (ssize_t)tlen || memcmp(t, tag, tlen) != 0)
			die("recorded values out of step");
		got = (size_t)hdr[1] << 24 | (size_t)hdr[2] << 16 | (size_t)hdr[3] << 8 | hdr[4];
		if (got != len || read(det_fd, val, len) != (ssize_t)len)
			die("recorded value has the wrong size");
	}
}

const char *amiga_det_str(const char *tag, const char *str)
{
	static char buf[1024];
	char tl[64];
	uint32 len = str ? strlen(str) : 0;

	if (mode != M_RECORD && !suppress)
		return str;
	snprintf(tl, sizeof tl, "%s#len", tag);
	amiga_det(tl, &len, sizeof len);
	if (len >= sizeof buf)
		die("recorded string too long");
	if (mode == M_RECORD)
		memcpy(buf, str ? str : "", len);
	amiga_det(tag, buf, len);
	buf[len] = '\0';
	return buf;
}

static char **make_env(const char *extra)
{
	char **env;
	int n = 0, i;

	while (environ[n])
		n++;
	if (!(env = malloc((n + 2) * sizeof (char *))))
		return NULL;
	for (i = 0; i < n; i++)
		env[i] = environ[i];
	env[n++] = (char *)extra;
	env[n] = NULL;
	return env;
}

pid_t amiga_fork_receiver(int error_pipe[2])
{
	static char var[MAXPATHLEN * 2 + 64];
	char **env;
	pid_t pid;

	if (suppress) {
		/* We have arrived where the parent forked: become the receiver,
		 * talking to the generator on the inherited pipe. */
		close(error_pipe[0]);
		close(error_pipe[1]);
		if (det_fd >= 0) {
			char name[MAXPATHLEN];
			close(det_fd);
			det_fd = -1;
			rec_name(name, sizeof name, "det");
			unlink(name);
		}
		error_pipe[0] = -1;
		error_pipe[1] = inherited_err;
		/* Writes and messages are live from here on.  Anything still left
		 * in a record is data the parent read ahead but never used, so
		 * amiga_read() keeps serving that first (mode stays M_REPLAY). */
		suppress = 0;
		trace("replay reached fork point; receiver running, err fd %d", inherited_err);
		return 0;
	}

	if (mode != M_RECORD || record_failed) {
		trace("fork emulation impossible: mode=%d record_failed=%d", mode, record_failed);
		errno = ENOSYS;
		return -1;
	}

	close_records(0);
	{
		int wp[2];
		if (pipe(wp) < 0)
			return -1;
		wrapup_fd = wp[0];
		wrapup_out = wp[1];
	}
	snprintf(var, sizeof var, REPLAY_ENV "=%s|%s|%d|%d|%d",
		 prefix, saved_cwd, error_pipe[1], noted_sock, wrapup_fd);
	if (!(env = make_env(var)))
		return -1;
	saved_argv[0] = self_exe;

	pid = vfork();
	if (pid == 0) {
		close(error_pipe[0]);
		close(wrapup_out);
		execve(self_exe, saved_argv, env);
		_exit(127);
	}
	close(wrapup_fd);
	wrapup_fd = -1;
	free(env);
	mode = M_NONE;
	trace("spawned receiver %ld (%s)", (long)pid, pid < 0 ? strerror(errno) : "ok");
	return pid;
}

pid_t amiga_piped_child(char **command, int *f_in, int *f_out, int blocking)
{
	int to_child[2], from_child[2];
	pid_t pid;

	if (pipe(to_child) < 0)
		return -1;
	if (pipe(from_child) < 0) {
		close(to_child[0]);
		close(to_child[1]);
		return -1;
	}

	pid = vfork();
	if (pid == 0) {
		/* Only async-safe calls between vfork() and exec. */
		if (dup2(to_child[0], STDIN_FILENO) < 0 || dup2(from_child[1], STDOUT_FILENO) < 0)
			_exit(127);
		if (to_child[0] != STDIN_FILENO)
			close(to_child[0]);
		if (from_child[1] != STDOUT_FILENO)
			close(from_child[1]);
		close(to_child[1]);
		close(from_child[0]);
		fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) & ~O_NONBLOCK);
		if (blocking)
			fcntl(STDOUT_FILENO, F_SETFL, fcntl(STDOUT_FILENO, F_GETFL) & ~O_NONBLOCK);
		execvp(command[0], command);
		_exit(127);
	}
	close(to_child[0]);
	close(from_child[1]);
	trace("piped child %s -> %ld", command[0], (long)pid);
	if (pid < 0) {
		close(to_child[1]);
		close(from_child[0]);
		return -1;
	}
	*f_in = from_child[0];
	*f_out = to_child[1];
	return pid;
}

pid_t amiga_local_child(char **server_args, int *f_in, int *f_out)
{
	server_args[0] = self_exe;
	return amiga_piped_child(server_args, f_in, f_out, 0);
}

pid_t amiga_spawn_daemon_conn(int fd, int *close_fds)
{
	pid_t pid;
	int i;

	saved_argv[0] = self_exe;
	pid = vfork();
	if (pid == 0) {
		if (fd != STDIN_FILENO) {
			if (dup2(fd, STDIN_FILENO) < 0)
				_exit(127);
			close(fd);
		}
		for (i = 0; close_fds && close_fds[i] >= 0; i++)
			close(close_fds[i]);
		execve(self_exe, saved_argv, environ);
		_exit(127);
	}
	return pid;
}

#endif /* __amigaos__ */
