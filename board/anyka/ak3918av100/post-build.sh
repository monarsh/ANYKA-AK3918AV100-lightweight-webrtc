#!/bin/sh
# Buildroot post-build hook. $1 = target directory.
set -e
TARGET_DIR="$1"

# The vendor sensor scripts copy ISP files into /etc/config, which the
# vendor firmware mounts from a jffs2 partition. Keep it as a plain
# directory on the read-only squashfs: tmpfs-backed via fstab below.
mkdir -p "$TARGET_DIR/etc/config"
grep -q ' /etc/config ' "$TARGET_DIR/etc/fstab" 2>/dev/null ||
	echo 'tmpfs /etc/config tmpfs mode=0755,size=1m 0 0' >> "$TARGET_DIR/etc/fstab"
