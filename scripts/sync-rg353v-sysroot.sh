#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <rg353v-host-or-ip> [ssh-user]"
  echo "Example: $0 192.168.1.42 root"
  exit 1
fi

HOST="$1"
USER="${2:-root}"
DEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/sysroot"
REMOTE="${USER}@${HOST}"
SSH_OPTS=(-o ConnectTimeout=5)

command -v rsync >/dev/null 2>&1 || { echo "rsync is required"; exit 1; }
command -v ssh >/dev/null 2>&1 || { echo "ssh is required"; exit 1; }

mkdir -p "${DEST_DIR}"

echo "Checking SSH connectivity to ${REMOTE}..."
ssh "${SSH_OPTS[@]}" "${REMOTE}" "echo ok" >/dev/null || {
  echo "SSH connection failed. Ensure SSH access to ${REMOTE} works first."
  exit 1
}

echo "Checking remote rsync availability..."
ssh "${SSH_OPTS[@]}" "${REMOTE}" "command -v rsync >/dev/null" || {
  echo "Remote host does not have rsync installed."
  echo "Install rsync on the RG353V, then run this script again."
  exit 1
}

echo "Syncing sysroot into: ${DEST_DIR}"

# Minimal dirs generally needed for cross-compiling/linking raylib DRM build.
for dir in /usr/include /usr/lib /lib /usr/lib64 /lib64; do
  if ssh "${SSH_OPTS[@]}" "${REMOTE}" "test -d ${dir}"; then
    echo "  -> ${dir}"
    mkdir -p "$(dirname "${DEST_DIR}${dir}")"
    rsync -a --delete "${REMOTE}:${dir}/" "${DEST_DIR}${dir}/"
  else
    echo "  -> ${dir} (missing on device, skipping)"
  fi
done

# Best effort: copy dynamic loader if present in an unusual path.
for loader in /lib/ld-linux-aarch64.so.1 /lib64/ld-linux-aarch64.so.1 /usr/lib/ld-linux-aarch64.so.1; do
  if ssh "${SSH_OPTS[@]}" "${REMOTE}" "test -f ${loader}"; then
    mkdir -p "$(dirname "${DEST_DIR}${loader}")"
    rsync -a "${REMOTE}:${loader}" "${DEST_DIR}${loader}"
  fi
done

echo
echo "Validating required headers in local sysroot..."
required_headers=(
  "/usr/include/GLES2/gl2.h"
  "/usr/include/EGL/egl.h"
  "/usr/include/libdrm/drm.h"
)

missing=0
for header in "${required_headers[@]}"; do
  if [[ -f "${DEST_DIR}${header}" ]]; then
    echo "  OK  ${header}"
  else
    echo "  MISS ${header}"
    missing=1
  fi
done

if [[ ${missing} -ne 0 ]]; then
  echo
  echo "Sysroot sync finished but required headers are missing."
  echo "Check transfer errors above and verify device paths/permissions."
  exit 1
fi

echo
echo "Sysroot sync complete."
echo "Set this once in your shell profile (~/.zshrc):"
echo "  export RG353V_SYSROOT=\"${DEST_DIR}\""
echo
echo "Then reconfigure:"
echo "  cmake --preset rg353v-release"
