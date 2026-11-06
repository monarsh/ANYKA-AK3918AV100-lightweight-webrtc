#!/bin/sh
#
# Build test of the external tree without the vendor SDK or toolchain:
# Buildroot + Ubuntu/Debian armel cross gcc + a mock SDK. Checks that the
# Kconfig, libpeer cross build and akwebrtc link all work. The image it
# produces is NOT usable on a camera.
#
#   sudo apt-get install gcc-arm-linux-gnueabi g++-arm-linux-gnueabi
#   scripts/build-test.sh <work-dir> [<public-sdk-checkout>]
#
set -eu

EXT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mkdir -p "$1" && cd "$1" && pwd)"
SDK_PUBLIC="${2:-$WORK/ak3918av100}"
BR_VERSION=2025.02.9

[ -d "$WORK/buildroot" ] ||
	git clone --depth 1 -b "$BR_VERSION" https://gitlab.com/buildroot.org/buildroot.git "$WORK/buildroot"
[ -d "$SDK_PUBLIC/platform/include" ] ||
	git clone --depth 1 https://github.com/KD0000001/ak3918av100.git "$SDK_PUBLIC"

# Debian's cross gcc keeps its target files in /usr/arm-linux-gnueabi
# ({include,lib}), while Buildroot expects a sysroot with usr/include and
# usr/lib. A "usr -> ." link inside it bridges the two (needs root once).
SYSROOT=/usr/arm-linux-gnueabi
if [ ! -e "$SYSROOT/usr" ]; then
	echo "build-test: creating $SYSROOT/usr -> . (test toolchain layout)"
	ln -s . "$SYSROOT/usr"
fi
# libc.so is a linker script naming /usr/arm-linux-gnueabi/lib/...; under
# --sysroot ld looks for that path inside the sysroot.
if [ ! -e "$SYSROOT/arm-linux-gnueabi" ]; then
	echo "build-test: creating $SYSROOT/arm-linux-gnueabi -> . (test toolchain layout)"
	ln -s . "$SYSROOT/arm-linux-gnueabi"
fi
TC="$WORK/armel-toolchain"
if [ ! -d "$TC/bin" ]; then
	mkdir -p "$TC/bin"
	for t in /usr/bin/arm-linux-gnueabi-*; do
		n="$(basename "$t")"
		case "$n" in
		*-gcc|*-g++|*-cpp|*-gcc-[0-9]*|*-g++-[0-9]*)
			# Buildroot refuses compilers built --with-sysroot=/ ; report
			# the wrapper's sysroot instead (test use only).
			cat > "$TC/bin/$n" <<-EOF
			#!/bin/sh
			if [ "\$*" = "-v" ]; then
			    $t -v 2>&1 | sed 's|--with-sysroot=/ |--with-sysroot=$SYSROOT |' >&2
			    exit 0
			fi
			exec $t --sysroot=$SYSROOT "\$@"
			EOF
			chmod +x "$TC/bin/$n" ;;
		*) ln -sf "$t" "$TC/bin/$n" ;;
		esac
	done
fi

"$EXT/scripts/make-mock-sdk.sh" "$SDK_PUBLIC" "$WORK/mock-sdk" arm-linux-gnueabi-gcc

OUT="$WORK/output"
make -C "$WORK/buildroot" O="$OUT" BR2_EXTERNAL="$EXT" anyka_ak3918av100_webrtc_defconfig
cat "$EXT/scripts/ci_debian_armel.config" >> "$OUT/.config"
sed -i \
	-e "s|^BR2_TOOLCHAIN_EXTERNAL_PATH=.*|BR2_TOOLCHAIN_EXTERNAL_PATH=\"$TC\"|" \
	-e '/^BR2_TOOLCHAIN_EXTERNAL_CUSTOM_UCLIBC=y/d' \
	-e '/^BR2_TOOLCHAIN_EXTERNAL_GCC_5=y/d' \
	-e '/^BR2_TOOLCHAIN_EXTERNAL_HEADERS_4_4=y/d' \
	"$OUT/.config"
echo "BR2_PACKAGE_ANYKA_AV100_SDK_PATH=\"$WORK/mock-sdk\"" >> "$OUT/.config"
make -C "$WORK/buildroot" O="$OUT" olddefconfig
# BUILD_TARGETS=akwebrtc builds only the WebRTC packages (no busybox,
# no image), which needs nothing but GitHub to download.
make -C "$WORK/buildroot" O="$OUT" ${BUILD_TARGETS:-}

file "$OUT/target/usr/bin/akwebrtc" "$OUT/target/usr/lib/libpeer.so"
[ -n "${BUILD_TARGETS:-}" ] || ls -l "$OUT/images/"
echo "build test OK"
