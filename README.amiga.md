# rsync for AmigaOS 3.x

This branch ports current rsync (3.5.x, protocol 33) to classic AmigaOS
3.1 and later on a 68020 or better, using ixemul.library.

## What works

- Local copies (`rsync -av Work:src/ DH1:backup/`), including delta
  transfers, `--delete`, `--checksum`, `--stats`, filters, compression.
- rsync daemon client: `rsync -av Work:src/ rsync://host/module/` and
  the other direction, against current and old rsync versions.
- rsync daemon server: `rsync --daemon` on the Amiga, used from Linux,
  macOS, Windows or another Amiga.

Tested in FS-UAE (A1200, 68030/68882, Kickstart 40.68, Workbench 40.42,
UAE bsdsocket.library) against rsync 3.5.1, 3.2.7, 3.1.3, 3.0.9 and
2.6.9 on Linux. Real hardware has not been tested yet.

## What does not work

- Transfers over a remote shell (`host:path` via ssh/rsh): there is no
  usable ssh for AmigaOS 3.x to start.
- `pre-xfer exec`, `post-xfer exec`, `early exec`, `name converter` and
  `RSYNC_CONNECT_PROG`: they need fork(). rsync reports an error.
- `max connections` in rsyncd.conf is not enforced (ixemul has no file
  record locking).
- Ownership, uid/gid mapping, `chroot`: AmigaOS has no users. rsync never
  runs as "root" here.
- ACLs, xattrs, iconv, IPv6.

## Installing

1. Copy `rsync` somewhere in your path, e.g. `C:`.
2. Install ixemul 80.1 (Aminet `dev/lib/ixemul-80.1-m68k.lha`). Copy a
   **matching pair** of libraries to `LIBS:`, renamed:

       Sys/libs/ixemul020.library  ->  LIBS:ixemul.library
       Sys/libs/ixnet020.library   ->  LIBS:ixnet.library

   (or the 040/060 pair for those CPUs). Do not use `ixnet000.library`
   with `ixemul000.library`: in 80.1 they are different build variants
   and networking silently stays off ("Function not implemented").
   rsync is built without FPU code, so use the non-`-fpu` ixemul.
3. A TCP/IP stack providing bsdsocket.library (Roadshow, AmiTCP, Miami,
   or UAE's emulated one).
4. ixnet opens `AmiTCP:libs/usergroup.library` in every ixemul program.
   If `AmiTCP:` does not exist you get a "Please insert volume AmiTCP"
   requester, so assign it, e.g. `Assign AmiTCP: SYS:` (the library
   itself is optional).
5. `T:` and `ENV:` must exist (the standard Startup-Sequence makes them).
6. Give rsync enough room in `T:`: see "How it works" below.

## Usage notes

- Paths can be written Unix style (`/Work/src`) or Amiga style
  (`Work:src`).
- rsync's exit codes are often 10 or more (for example 23 = partial
  transfer). AmigaDOS scripts stop at return codes from 10 on, so put
  `FailAt 1000` before rsync in scripts that should continue.
- Replacing a file is not atomic on AmigaOS: `Rename()` never overwrites,
  so rsync deletes the old file first. A crash right then leaves only
  the temporary `.name.XXXXXX` file.
- Daemon: start it with `Run >NIL: rsync --daemon --no-detach --config=...`.
  Use `use chroot = no` in rsyncd.conf.

## How it works

ixemul has no `fork()`, only `vfork()` followed by exec. rsync forks in
two places that matter:

- A local copy starts the receiving side as a separate `rsync --server`
  process. On the Amiga that is simply `vfork()` + exec of rsync itself,
  connected by pipes, and it is then treated exactly like a remote
  server.
- The receiving side splits into a generator and a receiver that run at
  the same time and talk over a pipe. Both need the state built before
  the split (options, protocol, filter rules, file list). rsync gets this
  by **record and replay**, the same idea as its own `--write-batch`:
  until the split the process records every byte it reads from a pipe or
  socket (plus the checksum seed and similar values) into `T:`. At the
  split it starts itself again; the new process replays the recording
  with its output suppressed, arrives in the same state and continues as
  the receiver on the inherited connection. Each part is an ordinary
  AmigaDOS process; nothing shares memory or DOS state across processes.

The recording is usually a few KB, but for transfers with very large
file lists it grows with the list, so `T:` needs space for it.

The generator tells the receiver to finish through a pipe rather than
SIGUSR2, and children are reaped with `waitpid()` rather than SIGCHLD.

## Troubleshooting

    SetEnv RSYNC_AMIGA_TRACE RAM:rsync-trace.txt

makes every rsync process append to that file what the fork emulation
does, all of rsync's messages, and the address of a crash. Setting
`RSYNC_AMIGA_KEEP` as well keeps the record files in `T:`.

## Building

Needs the m68k-amigaos cross toolchain from
<https://github.com/AmigaPorts/amiga-gcc> (`make min ndk`), then:

    PATH=/opt/amiga/bin:$PATH amiga/build.sh [build-dir]

`AMIGA_CPU=68040 amiga/build.sh` builds for another CPU. The Amiga
specific code is in `amiga/` and in `#ifdef __amigaos__` blocks.
