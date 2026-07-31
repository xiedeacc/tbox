#!/bin/bash

set -euo pipefail

SERVICE_NAME="tbox_server"
BINARY_NAME="tbox_server"
REMOTE="${REMOTE:-ubuntu@aws}"
INSTALL_DIR="/usr/local/tbox"
BIN_DIR="${INSTALL_DIR}/bin"
CONF_DIR="${INSTALL_DIR}/conf"
LOG_DIR="${INSTALL_DIR}/logs"
WORKSPACE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BAZEL_CONFIG="gcc_aarch64_linux_musl"
BAZEL_TARGET="//src/server:tbox_server"
LOCAL_BINARY="${WORKSPACE_ROOT}/bazel-bin/src/server/${BINARY_NAME}"

cd "${WORKSPACE_ROOT}"
bazel build --config="${BAZEL_CONFIG}" "${BAZEL_TARGET}"

if [[ ! -x "${LOCAL_BINARY}" ]]; then
    echo "Missing ${LOCAL_BINARY}; build --config=${BAZEL_CONFIG} ${BAZEL_TARGET} before deploying." >&2
    exit 1
fi

ssh "${REMOTE}" "sudo systemctl list-unit-files ${SERVICE_NAME}.service --no-legend | grep -q '^${SERVICE_NAME}.service'"
ssh "${REMOTE}" "sudo test -f ${CONF_DIR}/server_config.json"
ssh "${REMOTE}" "sudo mkdir -p ${BIN_DIR} ${CONF_DIR} ${LOG_DIR}"

TEMP_BINARY="$(mktemp /tmp/tbox_server.XXXXXX)"
TEMP_CONFIG="$(mktemp /tmp/tbox_server_config.XXXXXX)"
REMOTE_STAGE_DIR="$(ssh "${REMOTE}" "mktemp -d /tmp/tbox-server.XXXXXX")"
cleanup() {
    rm -f "${TEMP_BINARY}" "${TEMP_CONFIG}"
    ssh "${REMOTE}" \
        "rm -f '${REMOTE_STAGE_DIR}/${BINARY_NAME}' '${REMOTE_STAGE_DIR}/server_config.json'; rmdir '${REMOTE_STAGE_DIR}' 2>/dev/null || true" \
        >/dev/null 2>&1 || true
}
trap cleanup EXIT
cp "${LOCAL_BINARY}" "${TEMP_BINARY}"
if command -v llvm-strip >/dev/null 2>&1; then
    llvm-strip --strip-unneeded "${TEMP_BINARY}"
elif command -v aarch64-linux-gnu-strip >/dev/null 2>&1; then
    aarch64-linux-gnu-strip --strip-unneeded "${TEMP_BINARY}"
else
    echo "No strip tool found for ${BINARY_NAME}; refusing to deploy an unstripped binary." >&2
    exit 1
fi

python3 "${WORKSPACE_ROOT}/deploy/prepare_config.py" server \
    "${WORKSPACE_ROOT}/conf/server_config.json" "${TEMP_CONFIG}"
scp "${TEMP_BINARY}" "${REMOTE}:${REMOTE_STAGE_DIR}/${BINARY_NAME}"
scp "${TEMP_CONFIG}" "${REMOTE}:${REMOTE_STAGE_DIR}/server_config.json"
ssh "${REMOTE}" "chmod 755 '${REMOTE_STAGE_DIR}/${BINARY_NAME}' && chmod 600 '${REMOTE_STAGE_DIR}/server_config.json'"
ssh "${REMOTE}" "sudo install -m 755 '${REMOTE_STAGE_DIR}/${BINARY_NAME}' '${BIN_DIR}/${BINARY_NAME}.new' && sudo install -m 600 '${REMOTE_STAGE_DIR}/server_config.json' '${CONF_DIR}/server_config.json.new'"

SERVICE_USER="$(ssh "${REMOTE}" "sudo systemctl show -p User --value ${SERVICE_NAME}")"
if [[ -n "${SERVICE_USER}" ]]; then
    ssh "${REMOTE}" "sudo chown ${SERVICE_USER}:${SERVICE_USER} ${BIN_DIR}/${BINARY_NAME}.new ${CONF_DIR}/server_config.json.new ${LOG_DIR}"
fi

ssh "${REMOTE}" "sudo systemctl stop ${SERVICE_NAME}"
ssh "${REMOTE}" "sudo mv -f ${BIN_DIR}/${BINARY_NAME}.new ${BIN_DIR}/${BINARY_NAME}"
ssh "${REMOTE}" "sudo mv -f ${CONF_DIR}/server_config.json.new ${CONF_DIR}/server_config.json"
ssh "${REMOTE}" "sudo systemctl restart ${SERVICE_NAME}"
ssh "${REMOTE}" "sudo systemctl is-active --quiet ${SERVICE_NAME}"
echo "Deployed ${BINARY_NAME} to ${REMOTE}:${BIN_DIR}/${BINARY_NAME}"
