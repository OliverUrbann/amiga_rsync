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

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

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

#endif
