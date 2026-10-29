# rsync for AmigaOS 3.x

This branch ports current rsync (3.5.x, protocol 33) to classic AmigaOS
3.1 and later on a 68020 or better, using ixemul.library.

## Quick start: the command to use

For backups and restores between the Amiga and another machine, use
**`-rtX`**:

    rsync -rtX /Work/ rsync://server/module/work/       backup
    rsync -rtX rsync://server/module/work/ /Work/       restore

(add `-lH` if the disk contains soft or hard links, see "Links").

- `-r` recurses, `-t` keeps the dates (needed so the next run only sends
  changes), `-X` keeps the **file comments and protection bits**.
- **Without `-X`, restored files get their protection bits from the Unix
  mode on the server, and programs usually lose their `e` bit: they will
  no longer start.** Back up and restore with `-X`, both ways.
- **Do not use `-a` or `-p`.** They set the protection bits from the Unix
  mode *after* `-X` has set them, so `h`, `s`, `p`, `a` and usually `e`
  are lost again.
- `-X` needs rsync 3.0 or newer with xattr support on the other side
  (e.g. Ubuntu's rsync). With an older server, leave `-X` out and expect
  to fix protection bits by hand after a restore.

Details in "File comments and protection bits" below.

## What works

- Local copies (`rsync -rt /Work/src/ /DH1/backup/`), including delta
  transfers, `--delete`, `--checksum`, `--stats`, filters, compression.
- rsync daemon client: `rsync -rt /Work/src/ rsync://host/module/` and
  the other direction, against current and old rsync versions.
- File comments and protection bits, **only with `-X`**, see below.
- rsync daemon server: `rsync --daemon` on the Amiga, used from Linux,
  macOS, Windows or another Amiga.

Tested in FS-UAE (A1200, 68030/68882, Kickstart 40.68, Workbench 40.42,
UAE bsdsocket.library):

- local: first copy, no-op rerun, delta transfer with `--delete`,
  `--checksum`, a 400-file tree;
- against rsync 3.5.1, 3.2.7, 3.1.3, 3.0.9 and 2.6.9 on Linux: Amiga
  client push and pull to the Linux daemon, Linux client push and pull
  to the Amiga daemon, each with a first copy and a delta transfer with
  `--delete`;
- Amiga to Amiga (two emulator instances): module list, push and pull
  with `-z`, push with `--exclude` and `--delete-excluded`.

All results were byte-identical to the source. It also runs on real
hardware (Vampire, 68080).

Known issue: in one of four Amiga-to-Amiga runs, a push with `--exclude`
stalled right after the daemon accepted the connection. It did not
happen again in three repeats and is not understood yet.

## Tested options

Everything in this table was run in the emulator and the result checked
(contents, dates and, where noted, protection bits and comments). An
option that is not listed may well work; it has just not been tested
here yet. Paths are Unix style (`/Work/src/`), see "Usage notes".

Result:

- **works**: tested in every direction where it makes a difference.
- **partly**: works, but with a limitation given in the notes.
- **partly tested**: worked where it was tested; the notes say what is
  still missing.
- **avoid** / **not supported**: see the notes.

Tested: **L** local copy on the Amiga, **push** Amiga client to a Linux
daemon, **pull** Linux daemon to the Amiga client, **D** Linux client to
the Amiga daemon, **AA** Amiga to Amiga.

| Option / feature | Result | Tested | Notes |
|---|---|---|---|
| `-r -t` | partly | L, push, pull, D, AA | Contents and dates arrive, reruns only send changes, round trips give the original dates back. **But on a Linux server the dates are one hour ahead** (the time-zone offset), probably because ixemul takes the Amiga clock as UTC. Not tested yet: setting a time zone, summer time changes. |
| Local copy | works | L | `rsync -rt /Work/src/ /DH1/backup/`: first copy, rerun without changes, delta transfer. |
| Daemon client | works | push, pull | `rsync://host/module/...`; against rsync 3.5.1, 3.2.7, 3.1.3, 3.0.9 and 2.6.9. |
| Daemon server on the Amiga | works | D, AA | `rsync --daemon --no-detach --config=...` with `use chroot = no`. `max connections` is not enforced. |
| `-X` | works | L, push, pull, D | Comments and protection bits, on FFS and UAE's directory filesystem, see below. **Use it for backups.** |
| `-p` | partly | push, pull | Only `r`, `w`, `e` (and `d` together with `w`), owner bits only, see below. |
| `-l` | partly | L, push, pull | Soft links in both directions. Links sent from the Amiga have no date (1970 on Linux). See "Links". |
| `-H` | works | L, push, pull | Hard links. A hard-linked directory arrives as a directory with hard-linked files. |
| `-R` | partly tested | L | `-R /Work/src /Work/src2/sub` keeps the source path. Not tested with a daemon. |
| `-i` | works | L, push, pull, D | A changed comment or protection shows as `.f........x`. |
| `--delete` | works | L, push, pull, D | Together with the delta transfer. |
| `-c` (`--checksum`) | works | L, push, pull | Same size and date but other content: only `-c` sends the file. |
| `--exclude`, `--delete-excluded` | partly tested | L, push, AA | Pull not tested. |
| `--include`, `--exclude-from`, `-m` | partly tested | push | `--include=*/ --include=*.txt --exclude=*` with `-m`. Write patterns without quotes: the Amiga shell does not expand `*`. Local and pull not tested. |
| `--files-from=/RAM/list.txt` | partly tested | push | One path per line, relative to the source. Local and pull not tested. |
| `-z` (compression) | partly tested | L, AA | Push and pull between two Amigas. Not tested yet against Linux. |
| `-n` (`--dry-run`) | partly tested | push | Nothing created. With the Amiga as receiver (local, pull) not tested. |
| `--max-size`, `--min-size` | partly tested | push | With the Amiga as receiver not tested. |
| `--size-only`, `-u`, `--ignore-existing` | partly tested | push | Decided by the receiving side, here Linux; with the Amiga as receiver not tested. |
| `--delete-before`, `--delete-after` | partly tested | push | Deleting on the Amiga not tested with these. |
| `--backup --suffix=.bak` | partly tested | push | The old copy is kept as `name.bak` on Linux. Backups on the Amiga not tested (renaming works differently there, see "Usage notes"). |
| `--remove-source-files` | partly tested | push | The sent files are deleted on the Amiga. Pull not tested. |
| `--inplace`, `-W`, `--partial` | partly tested | push | With the Amiga as receiver not tested; `--partial` only with a complete transfer, not with an interrupted one. |
| `--bwlimit=500` | partly tested | push | 2 MB took 4 s at 500 KB/s. Pull not tested. |
| `-x` | partly tested | push | Only a tree on one volume; that rsync stops at other volumes is not tested. |
| `--timeout`, `--contimeout`, `--port` | partly tested | push | Accepted and the transfer works; no timeout was actually triggered. |
| `--password-file=/RAM/pw` | works | push | For modules with `auth users`: `rsync://user@host/module/`. A wrong password gives "auth failed", exit code 5. Without `--password-file` rsync asks on the terminal. |
| `--stats`, `-h`, `--log-file=RAM:r.log` | works | L, push | Output only. |
| `--progress` | works | L, push, pull | In a file the updates sit in one line, separated by carriage returns. |
| `--info=progress2` | works | pull | Total progress. |
| `--list-only`, `rsync rsync://host/` | works | push, AA | List a module's files, or the modules of a daemon. |
| `-a` | avoid | | It contains `-p`. For backups use `-rtX` instead. |
| Files of 2 GB or more | not supported | L, push | Skipped with an error, exit code 23. See "What does not work". |

To put a `*` into a file with the Amiga `Echo` command, double it: `Echo "**.tmp" >RAM:ex.txt` writes `*.tmp`.

### Protection bits: `-X` against `-p`

| | `-rtX` | `-rtp` |
|---|---|---|
| Comment | kept (`user.amiga.comment`) | lost |
| Protection `r`, `w`, `e`, `d` | kept exactly | `r`, `w`, `e` kept; `w` and `d` travel together |
| Protection `h`, `s`, `p`, `a` | kept | lost |
| Mode on the Linux server | new files get 0400 to 0700 from `r`, `w`, `e` as with `-p`; rsync leaves the mode of existing files alone | always set to 0400 to 0700, no group or other bits |
| Needs on the server | rsync 3.0+ with xattr support, writable module | nothing special |

Do not combine `-X` and `-p`: `-p` overwrites the bits that `-X` has just set.

What `-p` does, measured on an FFS volume (all 16 combinations of
`r`, `w`, `e` and `d`, plus 26 Linux modes in the other direction):

| Amiga to Linux | Linux mode |
|---|---|
| `r` | read (owner) |
| `w` or `d` | write (owner) |
| `e` | execute (owner) |
| (group and other) | no bits at all, e.g. 0700 |

| Linux to Amiga (owner bits count only) | Amiga |
|---|---|
| 0755, 0700 | `rwed` |
| 0644, 0600 | `rw-d` (no `e`: not executable) |
| 0555, 0500 | `r-e-` |
| 0444, 0400 | `r---` |
| 0100 | `--e-` |
| 0070, 0007, 0000 | nothing |

- The execute bit survives both ways, but a program on Linux needs the
  **owner** x bit, otherwise it does not start on the Amiga.
- Read and write are the same in both directions. A set Linux write bit
  gives `w` *and* `d`, so a file with `d` but without `w` (or the other
  way round) changes in a round trip. Directories always come back with
  `e`.
- Files without `r` cannot be read by rsync (there is no "root"): they
  are skipped with "Permission denied" and the exit code is 23.
- Reruns after a transfer with `-p` send nothing.

## What does not work

- Transfers over a remote shell (`host:path` via ssh/rsh): there is no
  usable ssh for AmigaOS 3.x to start.
- `pre-xfer exec`, `post-xfer exec`, `early exec`, `name converter` and
  `RSYNC_CONNECT_PROG`: they need fork(). rsync reports an error.
- `max connections` in rsyncd.conf is not enforced (ixemul has no file
  record locking).
- Ownership, uid/gid mapping, `chroot`: AmigaOS has no users. rsync never
  runs as "root" here.
- ACLs, Linux-style xattrs (other than the two below), iconv, IPv6.
- **Files of 2 GB or more.** AmigaOS 3.x and ixemul use 32-bit signed
  file sizes. rsync skips such files with "files of 2 GB or more are not
  supported on AmigaOS" and exits with code 23; everything else is
  transferred. (Before amiga3 they silently arrived empty.) Files of
  4 GB or more on PFS3/SFS may report a wrapped-around size and cannot
  be detected; keep such files out of rsync transfers.

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

## File comments and protection bits (`-X`)

AmigaDOS keeps a comment (`FileNote`) and protection bits (`hsparwed`,
`Protect`) with every file and directory. Unix filesystems have neither,
so by default rsync transfers only contents and modification times:

    rsync -rt /Work/ rsync://server/backup/work/      <- no comments, no bits

**Add `-X` to transfer them too.** The Amiga rsync then presents them as
two extended attributes, which a Linux server stores in its filesystem
(ext4, xfs, btrfs; the daemon module must be writable as usual):

    rsync -rtX /Work/ rsync://server/backup/work/

| On the Amiga        | On Linux (`getfattr -d file`)              |
|---------------------|--------------------------------------------|
| file comment        | `user.amiga.comment="Das List-Kommando"`   |
| protection bits     | `user.amiga.protection="-sp-rwed"`         |

The protection value is written the way `List` shows it: the letters
`hsparwed` in that order, `-` for a flag that is not set (for `rwed`:
not allowed). The default `----rwed` is not stored; a file without the
attribute gets `----rwed` when it comes back. Comments are stored as the
raw bytes (Latin-1), so umlauts look odd in a UTF-8 terminal but return
unchanged.

To restore, use `-X` again, in either direction:

    rsync -rtX rsync://server/backup/work/ /Work/            (on the Amiga)
    rsync -rtX /srv/backup/work/ rsync://amiga/module/       (on Linux)

Notes:

- **Both sides need `-X`**, which the client passes on to the server
  automatically. A server older than rsync 3.0 (protocol 30), or one
  built without xattr support, refuses `-X`; leave it out there.
- **Do not combine `-X` with `-p`.** `-p` makes ixemul turn the Unix mode
  into protection bits after the attributes were set, which loses `h`,
  `s`, `p` and `a` and usually `e`. `-X` alone is what you want.
- **Without `-X`, files that arrive on the Amiga get protection bits from
  their Unix mode via ixemul**, so a file without the Unix x bit loses
  `e` and a program no longer starts. Restore with `-X`. Files backed up
  without `-X` have no attributes on the server; restoring them with `-X`
  gives them the default `----rwed`.
- Other `user.*` attributes coming from Linux are ignored on the Amiga.
- Changing only a comment or the bits is detected: rsync itemizes it as
  `.f........x` and sends just the attribute.

Tested on UAE's directory filesystem and on an FFS volume: local copies,
push to and pull from a Linux daemon, Linux client to the Amiga daemon,
reruns without changes, comment-only changes, restoring a C: command and
running it.

## Links (`-l`, `-H`)

AmigaDOS has soft links and hard links (to files and to directories).
rsync handles them like on Unix, but only when asked:

- **Without `-l` and `-H`** (plain `-rt`/`-rtX`): soft links are skipped
  ("skipping non-regular file"), hard links are copied as independent
  files. A hard-linked directory is copied twice.
- **`-l`** transfers soft links as links, in both directions. Targets
  are converted between Amiga and Unix form by ixemul (`Work:x` <->
  `/Work/x`; relative targets stay relative). A link from Linux pointing
  to something like `/etc/hostname` becomes a dangling `etc:hostname`.
- **`-H`** keeps hard links to files, in both directions: on Linux they
  become ordinary hard links, on the Amiga real AmigaDOS hard links. A
  hard link to a **directory** (Unix has none) arrives as a directory of
  its own whose files are hard links to the originals, so nothing is
  stored twice, but the directory itself is no longer one object.

For a full backup use `rsync -rtXlH`. Soft links themselves carry no
comment or protection bits in the backup (only their targets do).

Notes: soft links sent from the Amiga carry no date (ixemul reports
none), so they show 1970 on Linux. A Linux daemon with `use chroot = no`
prefixes link targets with `/rsyncd-munged/` ("munge symlinks", a
security default); set `munge symlinks = no` in a trusted setup or use
`use chroot = yes`. Note that `MakeLink` of OS 3.1 creates hard links.

Tested on FFS: Amiga soft and hard links (file and directory) pushed to
a Linux daemon and copied locally, Linux symlinks (relative, absolute,
to a directory) and hard links pulled onto the Amiga, reruns without
changes.

## Linux files on an Amiga

The intended direction is the Amiga backing up to a Unix machine. The
other way round (a Linux client writing to an Amiga daemon, or pulling
Linux files onto the Amiga) keeps file **contents, the directory tree and
modification times**, but little else:

- Lost: owner and group (AmigaOS has no users), device files, FIFOs,
  ACLs and extended attributes (only the two `amiga.*` names above are
  kept). Symlinks and hard links survive with `-l`/`-H`, see above.
- Approximated: permissions with `-p` (ixemul maps `rwx` to `rwed`;
  group and other are dropped).

Some files cannot be stored correctly at all:

- Names differing only in case (`README` and `Readme`): AmigaDOS does not
  tell them apart, so one overwrites the other.
- Names longer than the filesystem allows (30 characters on FFS; more on
  PFS3, SFS or FFS2/`DOS\7`) fail.
- Names containing `:` cannot be created.
- Files of 2 GB or more: skipped with an error (see above).
- Dates before 1 January 1978, the start of Amiga time.

To keep Linux files intact on an Amiga, pack them on Linux first
(`tar czf backup.tar.gz ...`) and transfer the archive.

None of this was tested beyond plain files.

## Usage notes

- **Write paths Unix style, as for every ixemul program:** `/Work/src/`
  for `Work:src/`, `/RAM/` for `RAM:`, relative paths as usual. An Amiga
  path like `Work:src` does not work: rsync reads `name:path` as a
  remote host for ssh and fails with "remote command not found" (code
  127). With `-R`, the path is kept on the destination as `Work/src/...`.
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

## Planned

- **Files of 2 GB or more** on filesystems that can store them (PFS3,
  SFS): use their 64-bit DOS packets for size and seek instead of
  ixemul's 32-bit calls, keep the current error on FFS, and detect sizes
  of 4 GB or more reliably. Test with pfs3aio in FS-UAE.

## Troubleshooting

- `Empty destination arg specified (use "." or see --old-args)` for a
  correct command line: an old ixemul.library is installed. Check with
  `Version LIBS:ixemul.library`; it must be 80.1.
- `Unknown module 'x'`: the first part after `rsync://host/` is the
  module name from the server's rsyncd.conf. `rsync rsync://host/` lists
  the modules.

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
