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
REMOTE_BOLT_LOG_PATH="${RG353V_BOLT_LOG_PATH:-/roms2/ports/bolt/bolt.log}"
LOCAL_DIR="./profiling-logs"
LOCAL_PROFILE_PREFIX="handheld-profile-"
LOCAL_BOLT_PREFIX="handheld-bolt-"

if [[ -z "${DEVICE_IP}" ]]; then
  echo "RG353V device IP is missing."
  echo "Pass it as 1st arg or set RG353V_DEVICE_IP."
  exit 1
fi

mkdir -p "${LOCAL_DIR}"

next_index=1
for file in "${LOCAL_DIR}/${LOCAL_PROFILE_PREFIX}"*.log "${LOCAL_DIR}/${LOCAL_BOLT_PREFIX}"*.log; do
  [[ -e "${file}" ]] || continue
  filename="$(basename "${file}")"
  if [[ "${filename}" =~ ^(${LOCAL_PROFILE_PREFIX}|${LOCAL_BOLT_PREFIX})([0-9]+)\.log$ ]]; then
    index="${BASH_REMATCH[2]}"
    if (( index >= next_index )); then
      next_index=$((index + 1))
    fi
  fi
done

LOCAL_PROFILE_TARGET="${LOCAL_DIR}/${LOCAL_PROFILE_PREFIX}${next_index}.log"
LOCAL_BOLT_TARGET="${LOCAL_DIR}/${LOCAL_BOLT_PREFIX}${next_index}.log"

echo "Checking remote profile file: ${REMOTE_PROFILE_PATH}"
if ! ssh "${DEVICE_USER}@${DEVICE_IP}" "test -f '${REMOTE_PROFILE_PATH}'"; then
  echo "Remote profile file not found: ${REMOTE_PROFILE_PATH}"
  exit 1
fi

echo "Copying profile log to ${LOCAL_PROFILE_TARGET}"
scp "${DEVICE_USER}@${DEVICE_IP}:${REMOTE_PROFILE_PATH}" "${LOCAL_PROFILE_TARGET}"

if ssh "${DEVICE_USER}@${DEVICE_IP}" "test -f '${REMOTE_BOLT_LOG_PATH}'"; then
  echo "Copying bolt log to ${LOCAL_BOLT_TARGET}"
  scp "${DEVICE_USER}@${DEVICE_IP}:${REMOTE_BOLT_LOG_PATH}" "${LOCAL_BOLT_TARGET}"
else
  echo "Remote bolt log not found: ${REMOTE_BOLT_LOG_PATH}"
  echo "Skipping bolt log copy."
fi

echo "Removing remote profile and bolt logs..."
if ! ssh "${DEVICE_USER}@${DEVICE_IP}" "rm -f '${REMOTE_PROFILE_PATH}' '${REMOTE_BOLT_LOG_PATH}'"; then
  echo "Copied logs, but failed to remove one or more remote files."
  exit 1
fi

echo "Logs copied and removed from device:"
echo "  profile -> ${LOCAL_PROFILE_TARGET}"
if [[ -f "${LOCAL_BOLT_TARGET}" ]]; then
  echo "  bolt    -> ${LOCAL_BOLT_TARGET}"
fi
