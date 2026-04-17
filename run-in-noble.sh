#!/usr/bin/env bash
set -euo pipefail

ROOTFS=/opt/ubuntu-noble
if [[ ! -d "$ROOTFS" ]]; then
  echo "Missing $ROOTFS. Create it with debootstrap first." >&2
  exit 1
fi

if [[ ! -d /home ]]; then
  echo "Host /home is missing." >&2
  exit 1
fi

if ! mountpoint -q "$ROOTFS/home"; then
  mount --bind /home "$ROOTFS/home"
fi

if [[ $# -eq 0 ]]; then
  echo "Usage: $0 <command...>" >&2
  echo "Example: $0 /home/study/FIIT_SP/build-check/allocator/allocator_sorted_list/tests/sys_prog_allctr_allctr_srtd_lst_tests" >&2
  exit 2
fi

chroot "$ROOTFS" /bin/bash -lc "$*"
