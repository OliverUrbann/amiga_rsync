/*
 * The few direct dos.library calls of the AmigaOS port.  Kept apart from
 * amiga.c because the NDK headers and rsync's/ixemul's headers don't mix.
 *
 * Copyright (C) 2026 Oliver Urbann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#ifdef __amigaos__

/* Private names, so nothing clashes with whatever ixemul's libc defines. */
#define SysBase amiga_SysBase
#define DOSBase amiga_DOSBase

#include <exec/types.h>
#include <exec/execbase.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

static int dos_open(void)
{
	SysBase = *(struct ExecBase **)4L;
	DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 36);
	return DOSBase != NULL;
}

/* Name of the current directory as an Amiga path ("Work:sub").  ixemul's
 * getcwd() needs a CLI, which a process started via vfork()+exec lacks. */
int amiga_cwd_name(char *buf, int len)
{
	struct Process *me;
	int ok;

	SysBase = *(struct ExecBase **)4L;
	if (!(DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 36)))
		return 0;
	me = (struct Process *)FindTask(NULL);
	if (me->pr_CurrentDir)
		ok = NameFromLock(me->pr_CurrentDir, (STRPTR)buf, len) != 0;
	else if ((ok = len > 4)) /* no lock means the boot volume's root */
		CopyMem((APTR)"SYS:", buf, 5);
	CloseLibrary((struct Library *)DOSBase);
	return ok;
}

/*
 * Amiga file comments and protection bits for rsync's -X.
 *
 * which: 0 = comment, 1 = protection.  The protection is exchanged as the
 * 8 letters "hsparwed" in that order, a letter meaning the flag is set
 * (for r, w, e and d: the access is allowed), '-' otherwise, the way the
 * List command shows it.  Path is an AmigaDOS path.
 *
 * Results: >= 0 length (get) or 0 (set); AXA_NOATTR the file has no such
 * value; AXA_NOENT the object is not there; AXA_RANGE buffer too small;
 * AXA_INVAL bad value; AXA_IO other failure.
 */
#define AXA_NOATTR -1
#define AXA_NOENT  -2
#define AXA_RANGE  -3
#define AXA_INVAL  -4
#define AXA_IO     -5

static const char prot_letters[] = "hsparwed";

static void prot_to_text(LONG prot, char *t)
{
	int i;
	for (i = 0; i < 8; i++) {
		int bit = 7 - i;	/* h=7 s=6 p=5 a=4 r=3 w=2 e=1 d=0 */
		int set = (prot >> bit) & 1;
		if (bit < 4)
			set = !set;	/* RWED are stored inverted: 1 = denied */
		t[i] = set ? prot_letters[i] : '-';
	}
}

static int text_to_prot(const char *t, int len, LONG *prot)
{
	LONG p = 0;
	int i;
	if (len != 8)
		return 0;
	for (i = 0; i < 8; i++) {
		int bit = 7 - i, set;
		if (t[i] == prot_letters[i] || t[i] == prot_letters[i] - 32)
			set = 1;
		else if (t[i] == '-')
			set = 0;
		else
			return 0;
		if (bit < 4)
			set = !set;
		if (set)
			p |= 1L << bit;
	}
	*prot = p;
	return 1;
}

/* No "Please insert volume" requester while rsync works unattended:
 * a missing volume is an error, not a reason to wait for the user. */
static APTR no_requesters(void)
{
	struct Process *me = (struct Process *)FindTask(NULL);
	APTR old = me->pr_WindowPtr;
	me->pr_WindowPtr = (APTR)-1;
	return old;
}

static void restore_requesters(APTR old)
{
	((struct Process *)FindTask(NULL))->pr_WindowPtr = old;
}

static int examine(const char *path, struct FileInfoBlock *fib)
{
	APTR win = no_requesters();
	BPTR lock = Lock((CONST_STRPTR)path, SHARED_LOCK);
	LONG err = IoErr();
	int ok;
	restore_requesters(win);
	if (!lock)
		return err == ERROR_OBJECT_NOT_FOUND || err == ERROR_DIR_NOT_FOUND
		     || err == ERROR_DEVICE_NOT_MOUNTED ? AXA_NOENT : AXA_IO;
	ok = Examine(lock, fib);
	UnLock(lock);
	return ok ? 0 : AXA_IO;
}

/* Default protection of a new file: ----rwed, i.e. all bits 0. */
int amiga_xa_get(const char *path, int which, char *buf, int size)
{
	struct FileInfoBlock *fib;
	int ret, len;
	char t[8];

	if (!dos_open())
		return AXA_IO;
	if (!(fib = AllocDosObject(DOS_FIB, NULL))) {
		CloseLibrary((struct Library *)DOSBase);
		return AXA_IO;
	}
	if ((ret = examine(path, fib)) == 0) {
		if (which == 0) {
			len = 0;
			while (len < 80 && fib->fib_Comment[len])
				len++;
			if (!len)
				ret = AXA_NOATTR;
			else if (size == 0)
				ret = len;
			else if (size < len)
				ret = AXA_RANGE;
			else {
				CopyMem(fib->fib_Comment, buf, len);
				ret = len;
			}
		} else {
			LONG prot = fib->fib_Protection & 0xff;
			if (!prot)
				ret = AXA_NOATTR;	/* the default needs no attribute */
			else if (size == 0)
				ret = 8;
			else if (size < 8)
				ret = AXA_RANGE;
			else {
				prot_to_text(prot, t);
				CopyMem(t, buf, 8);
				ret = 8;
			}
		}
	}
	FreeDosObject(DOS_FIB, fib);
	CloseLibrary((struct Library *)DOSBase);
	return ret;
}

/* val == NULL removes the value (empty comment, default protection). */
int amiga_xa_set(const char *path, int which, const char *val, int len)
{
	struct FileInfoBlock *fib;
	int ret;

	if (!dos_open())
		return AXA_IO;
	if (!(fib = AllocDosObject(DOS_FIB, NULL))) {
		CloseLibrary((struct Library *)DOSBase);
		return AXA_IO;
	}
	if ((ret = examine(path, fib)) == 0) {
		if (which == 0) {
			char c[80];
			if (len > 79)
				len = 79;	/* AmigaDOS comments hold 79 characters */
			if (val)
				CopyMem((APTR)val, c, len);
			c[val ? len : 0] = '\0';
			ret = SetComment((CONST_STRPTR)path, (CONST_STRPTR)c) ? 0 : AXA_IO;
		} else {
			LONG prot = 0;
			if (val && !text_to_prot(val, len, &prot))
				ret = AXA_INVAL;
			else {
				/* keep the bits above the low 8 (multi-user extensions) */
				prot |= fib->fib_Protection & ~0xffL;
				ret = SetProtection((CONST_STRPTR)path, prot) ? 0 : AXA_IO;
			}
		}
	}
	FreeDosObject(DOS_FIB, fib);
	CloseLibrary((struct Library *)DOSBase);
	return ret;
}

#endif
