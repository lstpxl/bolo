#!/usr/bin/env bash
set -euo pipefail

if [[ -f ".env" ]]; then
  set -a
  # shellcheck disable=SC1091
  source ".env"
  set +a
fi

if [[ $# -lt 1 ]]; then
  echo "Usage: bash scripts/deploy-rg353v.sh <release|debug> [device_ip]"
  exit 1
fi

BUILD_FLAVOR="$1"
DEVICE_IP="${2:-${RG353V_DEVICE_IP:-}}"
DEVICE_USER="${RG353V_DEVICE_USER:-ark}"
REMOTE_DIR="${RG353V_REMOTE_DIR:-/roms2/ports/bolo}"

if [[ -z "${DEVICE_IP}" ]]; then
  echo "RG353V device IP is missing."
  echo "Pass it as 2nd arg or set RG353V_DEVICE_IP."
  exit 1
fi

case "${BUILD_FLAVOR}" in
  release|debug)
    ;;
  *)
    echo "Invalid build flavor: ${BUILD_FLAVOR}. Use release or debug."
    exit 1
    ;;
esac

LOCAL_BINARY="./build/rg353v-${BUILD_FLAVOR}/bolo"
LOCAL_RESOURCES_DIR="./resources"

if [[ ! -f "${LOCAL_BINARY}" ]]; then
  echo "Binary not found: ${LOCAL_BINARY}"
  echo "Run the matching build script first."
  exit 1
fi

if [[ ! -d "${LOCAL_RESOURCES_DIR}" ]]; then
  echo "Resources directory not found: ${LOCAL_RESOURCES_DIR}"
  exit 1
fi

echo "Creating remote directory: ${REMOTE_DIR}"
ssh "${DEVICE_USER}@${DEVICE_IP}" "mkdir -p '${REMOTE_DIR}'"

echo "Uploading binary..."
scp "${LOCAL_BINARY}" "${DEVICE_USER}@${DEVICE_IP}:${REMOTE_DIR}/bolo"

echo "Syncing resources..."
rsync -av --delete "${LOCAL_RESOURCES_DIR}/" "${DEVICE_USER}@${DEVICE_IP}:${REMOTE_DIR}/resources/"

echo "Deploy complete:"
echo "  ${DEVICE_USER}@${DEVICE_IP}:${REMOTE_DIR}"
