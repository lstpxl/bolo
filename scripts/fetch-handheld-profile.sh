#!/usr/bin/env bash
set -euo pipefail

if [[ -f ".env" ]]; then
  set -a
  # shellcheck disable=SC1091
  source ".env"
  set +a
fi

if [[ $# -gt 1 ]]; then
  echo "Usage: bash scripts/fetch-handheld-profile.sh [device_ip]"
  exit 1
fi

DEVICE_IP="${1:-${RG353V_DEVICE_IP:-}}"
DEVICE_USER="${RG353V_DEVICE_USER:-ark}"
REMOTE_PROFILE_PATH="${RG353V_PROFILE_LOG_PATH:-/roms2/ports/bolt/profile.log}"
LOCAL_DIR="./profiling-logs"
LOCAL_PREFIX="handheld-profile-"

if [[ -z "${DEVICE_IP}" ]]; then
  echo "RG353V device IP is missing."
  echo "Pass it as 1st arg or set RG353V_DEVICE_IP."
  exit 1
fi

mkdir -p "${LOCAL_DIR}"

next_index=1
for file in "${LOCAL_DIR}/${LOCAL_PREFIX}"*.log; do
  [[ -e "${file}" ]] || continue
  filename="$(basename "${file}")"
  if [[ "${filename}" =~ ^${LOCAL_PREFIX}([0-9]+)\.log$ ]]; then
    index="${BASH_REMATCH[1]}"
    if (( index >= next_index )); then
      next_index=$((index + 1))
    fi
  fi
done

LOCAL_TARGET="${LOCAL_DIR}/${LOCAL_PREFIX}${next_index}.log"

echo "Checking remote profile file: ${REMOTE_PROFILE_PATH}"
if ! ssh "${DEVICE_USER}@${DEVICE_IP}" "test -f '${REMOTE_PROFILE_PATH}'"; then
  echo "Remote profile file not found: ${REMOTE_PROFILE_PATH}"
  exit 1
fi

echo "Copying profile log to ${LOCAL_TARGET}"
scp "${DEVICE_USER}@${DEVICE_IP}:${REMOTE_PROFILE_PATH}" "${LOCAL_TARGET}"

echo "Removing remote profile file..."
if ! ssh "${DEVICE_USER}@${DEVICE_IP}" "rm -f '${REMOTE_PROFILE_PATH}'"; then
  echo "Copied to ${LOCAL_TARGET}, but failed to remove remote file."
  exit 1
fi

echo "Profile log copied and removed from device:"
echo "  ${LOCAL_TARGET}"
