#!/bin/bash
set -euo pipefail

IMG="/tmp/rocknix_xu10.img"
cp -f "/Users/oleksii/Documents/XU10 Groovebox/backups/ROCKNIX-RK3326.aarch64-20261001-a.img" "${IMG}"
DISK="/dev/disk4"
RDISK="/dev/rdisk4"

# Safety check: verify /dev/disk4 is external and ~31-32 GB
DISK_INFO=$(diskutil info "${DISK}")
if ! echo "${DISK_INFO}" | grep -q "Device Location:.*External"; then
  echo "ERROR: ${DISK} is not an external disk! Aborting." >&2
  exit 1
fi

echo "Unmounting ${DISK}..."
diskutil unmountDisk "${DISK}"

echo "Flashing ${IMG} to ${RDISK} (macOS administrator prompt will appear)..."
osascript -e "do shell script \"dd if='${IMG}' of='${RDISK}' bs=4m conv=sync && sync && diskutil unmountDisk '${DISK}'\" with administrator privileges"

echo "Flash complete and ${DISK} unmounted cleanly."
