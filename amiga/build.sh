#!/bin/sh
# Cross-build rsync for AmigaOS 3.x (68020+, ixemul.library).
#
# Needs the m68k-amigaos toolchain from https://github.com/AmigaPorts/amiga-gcc
# built with ixemul support ("make min ndk"), its bin/ directory in PATH.
#
# Usage: amiga/build.sh [build-dir]       (default: build-amiga)
set -e

srcdir=$(cd "$(dirname "$0")/.." && pwd)
builddir=${1:-build-amiga}
cpu=${AMIGA_CPU:-68020}

command -v m68k-amigaos-gcc >/dev/null || {
	echo "m68k-amigaos-gcc not found in PATH" >&2
	exit 1
}

mkdir -p "$builddir"
cd "$builddir"

# Answers for checks configure cannot run when cross-compiling, and for
# functions ixemul links but does not implement.
cat > amiga.cache <<'EOF'
ac_cv_func_socketpair=no
ac_cv_type_socklen_t=yes
ac_cv_func_strlcpy=no
ac_cv_func_strlcat=no
ac_cv_func_mkfifo=no
ac_cv_func_mknod=no
ac_cv_func_chroot=no
ac_cv_func_fork=no
ac_cv_func_lchmod=no
ac_cv_func_lutimes=no
ac_cv_func_utimensat=no
ac_cv_func_futimens=no
ac_cv_func_posix_fallocate=no
ac_cv_func_fallocate=no
ac_cv_func_mallinfo=no
ac_cv_func_mallinfo2=no
ac_cv_func_getgrouplist=no
ac_cv_func_initgroups=no
ac_cv_func_setgroups=no
rsync_cv_HAVE_SOCKETPAIR=no
rsync_cv_HAVE_SECURE_MKSTEMP=yes
rsync_cv_HAVE_C99_VSNPRINTF=yes
rsync_cv_HAVE_GETTIMEOFDAY_TZ=yes
rsync_cv_REPLACE_INET_NTOA=no
rsync_cv_REPLACE_INET_ATON=no
rsync_cv_HAVE_BROKEN_READDIR=no
rsync_cv_HAVE_LONGLONG=yes
rsync_cv_SIZEOF_OFF64_T=0
rsync_cv_HAVE_OFF64_T=no
rsync_cv_HAVE_SHORT_INO_T=no
rsync_cv_HAVE_UNSIGNED_CHAR=no
rsync_cv_can_hardlink_symlink=no
rsync_cv_can_hardlink_special=no
rsync_cv_MAKEDEV_TAKES_3_ARGS=no
rsync_cv_have_sockaddr_un=no
EOF

CC="m68k-amigaos-gcc -mcrt=ixemul -m$cpu" \
CFLAGS="-std=gnu17 -fgnu89-inline -DENOTSUP=EOPNOTSUPP -DEOVERFLOW=ERANGE ${CFLAGS:--O2 -fomit-frame-pointer}" \
"$srcdir/configure" --host=m68k-amigaos --cache-file=amiga.cache \
	--disable-ipv6 --disable-xxhash --disable-zstd --disable-lz4 \
	--disable-openssl --disable-iconv --disable-iconv-open \
	--disable-acl-support --disable-xattr-support --disable-locale \
	--disable-md2man --disable-idn --disable-roll-simd --disable-md5-asm \
	--disable-roll-asm --with-included-zlib --with-included-popt

make -j"$(nproc 2>/dev/null || echo 2)" rsync
m68k-amigaos-strip -o rsync.stripped rsync 2>/dev/null || true
echo
echo "Built $(pwd)/rsync"
