/*
 * AmigaOS 3.x (m68k, ixemul) support for rsync.
 *
 * ixemul.library has no fork(), only vfork() followed by exec.  rsync forks
 * in a few places; the one that matters is the split of the receiving side
 * into generator and receiver (do_recv() in main.c), where both halves need
 * the state built up before the fork: options, negotiated protocol, filter
 * rules and the file list.
 *
 * We emulate that fork by record and replay, the same idea rsync's own
 * --write-batch/--read-batch use: until the fork point, the process records
 * every byte it reads from a pipe or socket (plus the few values that are
 * not reproducible, like the checksum seed or an auth challenge).  At the
 * fork point it vfork()s and execs itself with the original command line.
 * The new process runs the very same code, reading the recorded bytes
 * instead of the live connection and discarding everything it would write,
 * and so arrives at the fork point in the same state.  There it takes the
 * receiver's branch and switches over to the live file descriptors it
 * inherited.
 *
 * See README.amiga.md for what this means for users.
 */

#ifndef RSYNC_AMIGA_H
#define RSYNC_AMIGA_H

#include <sys/types.h>
#include <poll.h>
#include <fcntl.h>

/* ixemul has no O_NOFOLLOW.  Without it the symlink-race hardening of the
 * sender and receiver falls back to plain opens; on a single-user Amiga
 * there is no other local user to race against. */
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

/* Only passed to do_unlink_atfd(), which is a stub without *at() calls. */
#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif

void amiga_init(int argc, char *argv[]);
void amiga_cleanup(void);

ssize_t amiga_read(int fd, void *buf, size_t len);
ssize_t amiga_write(int fd, const void *buf, size_t len);
int amiga_poll(struct pollfd *fds, nfds_t nfds, int timeout);

/* True while a replaying child is still before its fork point. */
int amiga_replaying(void);

/* Stop recording; called once we know this process won't receive files. */
void amiga_stop_recording(void);

/* Remember an fd the parent opened that a replaying child must reuse
 * instead of opening its own (e.g. the daemon socket of a client). */
void amiga_note_fd(const char *what, int fd);
int amiga_inherited_fd(const char *what);

/* Record (parent) or replay (child) a value that is not reproducible. */
void amiga_det(const char *tag, void *val, size_t len);
const char *amiga_det_str(const char *tag, const char *str);

/* The emulated fork() of do_recv(): returns the child pid in the parent,
 * 0 in the (replayed) child, -1 on error.  error_pipe is adjusted in the
 * child to the inherited descriptor. */
pid_t amiga_fork_receiver(int error_pipe[2]);

/* vfork()+exec a command with stdin/stdout connected to new pipes. */
pid_t amiga_piped_child(char **command, int *f_in, int *f_out, int blocking);

/* Start this rsync again as a local --server child (replaces local_child). */
pid_t amiga_local_child(char **server_args, int *f_in, int *f_out);

/* Start this rsync again to serve one daemon connection on fd (inetd style). */
pid_t amiga_spawn_daemon_conn(int fd, int *close_fds);

const char *amiga_self_exe(void);

/* ixemul's signal delivery is not something to build on (it crashes under
 * AROS), so the generator's end-of-run SIGUSR2 to the receiver travels
 * through a pipe instead. */
int amiga_tell_receiver_wrapup(pid_t pid);
void amiga_check_wrapup(void);
void amiga_trace_msg(int code, const char *buf, int len);
int amiga_cwd_name(char *buf, int len); /* amiga_dos.c */
long amiga_ix_gmt_offset(void);         /* amiga_dos.c */

/* File times between ixemul and real Unix time, see amiga.c. */
#include <time.h>
time_t amiga_time_from_fs(time_t t);
time_t amiga_time_to_fs(time_t t);

/* ixemul declares getgroups(int, int *); configure assumes gid_t when cross
 * compiling. */
#undef GETGROUPS_T
#define GETGROUPS_T int

/* ixemul has localtime() but not localtime_r(); rsync is single-threaded. */
#include <time.h>
#include <string.h>
static inline struct tm *amiga_localtime_r(const time_t *t, struct tm *res)
{
	struct tm *tm = localtime(t);
	if (!tm)
		return NULL;
	memcpy(res, tm, sizeof *res);
	return res;
}
#define localtime_r amiga_localtime_r

/* No *at() calls in ixemul.  rsync only reaches these with a held dir fd,
 * which it never has on Amiga (dir fds are always -1), but the xattr code
 * still has to compile. */
#include <errno.h>
static inline int amiga_openat(int dfd, const char *path, int flags, ...)
{
	(void)dfd; (void)path; (void)flags;
	errno = ENOSYS;
	return -1;
}
#define openat amiga_openat

/* "no such attribute" for the xattr code; ixemul has no ENOATTR/ENODATA. */
#ifndef ENOATTR
#define ENOATTR 93
#endif

/* The current time, corrected like file times when TZ is set (log time
 * stamps, "now" for comparisons). */
time_t amiga_time(time_t *t);

#ifndef AMIGA_NO_WRAP
#define time(t) amiga_time(t)
#undef read
#undef write
#undef poll
#define read(fd, buf, len) amiga_read(fd, buf, len)
#define write(fd, buf, len) amiga_write(fd, buf, len)
#define poll(fds, nfds, timeout) amiga_poll(fds, nfds, timeout)
#endif

#endif
