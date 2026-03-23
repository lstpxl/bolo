#!/usr/bin/env bash
set -euo pipefail

if [[ -f ".env" ]]; then
  set -a
  # shellcheck disable=SC1091
  source ".env"
  set +a
fi

if [[ $# -gt 1 ]]; then
  echo "Usage: bash scripts/remove-handheld-profile.sh [device_ip]"
  exit 1
fi

DEVICE_IP="${1:-${RG353V_DEVICE_IP:-}}"
DEVICE_USER="${RG353V_DEVICE_USER:-ark}"
REMOTE_PROFILE_PATH="${RG353V_PROFILE_LOG_PATH:-/roms2/ports/bolt/profile.log}"
REMOTE_BOLT_LOG_PATH="${RG353V_BOLT_LOG_PATH:-/roms2/ports/bolt/bolt.log}"

if [[ -z "${DEVICE_IP}" ]]; then
  echo "RG353V device IP is missing."
  echo "Pass it as 1st arg or set RG353V_DEVICE_IP."
  exit 1
fi

echo "Removing remote log files:"
echo "  ${REMOTE_PROFILE_PATH}"
echo "  ${REMOTE_BOLT_LOG_PATH}"
if ssh "${DEVICE_USER}@${DEVICE_IP}" "rm -f '${REMOTE_PROFILE_PATH}' '${REMOTE_BOLT_LOG_PATH}'"; then
  echo "Profile and bolt logs removed from device."
else
  echo "Failed to remove one or more remote log files."
  exit 1
fi
