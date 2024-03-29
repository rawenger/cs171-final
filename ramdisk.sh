#!/bin/bash

set -e

MOUNT_DIR="./ramfs"
mkdir -p "$MOUNT_DIR"
MOUNT_DIR="$(realpath "$MOUNT_DIR")"

[ "$(mount | grep -o "$MOUNT_DIR")" = "$MOUNT_DIR" ] && exit 0


if [ "$(uname)" = "Linux" ]; then
  sudo mount -t ramfs -o size=1g ramfs "$MOUNT_DIR"  # size parameter seems to be ignored here?
  sudo chown -R "$(whoami)":"$(whoami)" "$MOUNT_DIR"
  mount | grep ram
  echo "To unmount and free memory run \`sudo umount $(basename "$MOUNT_DIR")\`"
else
  SIZE=$((1024 * 1024 * 1024))
  DEVICE=$(hdiutil attach -nomount ram://$(($SIZE / 512)) | xargs)
  newfs_hfs -v cs171-ramdisk "$DEVICE"
  diskutil mount -mountPoint "$MOUNT_DIR" "$DEVICE"
  mount | grep ram
  echo "To unmount and free memory run \`diskutil eject $(basename "$MOUNT_DIR")\` or \`hdiutil detach '$DEVICE'\`"
fi