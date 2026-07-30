#!/bin/bash

set -euo pipefail

SERVICE_NAME="tbox_client"
BINARY_NAME="tbox_client"
REMOTE="root@nas"
INSTALL_DIR="/opt/usr/local/tbox"
BIN_DIR="${INSTALL_DIR}/bin"
CONF_DIR="${INSTALL_DIR}/conf"
LOG_DIR="${INSTALL_DIR}/logs"
WORKSPACE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BAZEL_TARGET="//src/client:tbox_client"
LOCAL_BINARY="${WORKSPACE_ROOT}/bazel-bin/src/client/${BINARY_NAME}"

cd "${WORKSPACE_ROOT}"
bazel build "${BAZEL_TARGET}"

if [[ ! -x "${LOCAL_BINARY}" ]]; then
    echo "Missing ${LOCAL_BINARY}; build ${BAZEL_TARGET} before deploying." >&2
    exit 1
fi

ssh "${REMOTE}" "systemctl list-unit-files ${SERVICE_NAME}.service --no-legend | grep -q '^${SERVICE_NAME}.service'"
ssh "${REMOTE}" "test -f ${CONF_DIR}/client_config.json"
ssh "${REMOTE}" "mkdir -p ${BIN_DIR} ${LOG_DIR}"

TEMP_BINARY="$(mktemp /tmp/tbox_client.XXXXXX)"
trap 'rm -f "${TEMP_BINARY}"' EXIT
cp "${LOCAL_BINARY}" "${TEMP_BINARY}"
if command -v llvm-strip >/dev/null 2>&1; then
    llvm-strip --strip-unneeded "${TEMP_BINARY}"
else
    strip --strip-unneeded "${TEMP_BINARY}"
fi

scp "${TEMP_BINARY}" "${REMOTE}:${BIN_DIR}/${BINARY_NAME}.new"
scp "${WORKSPACE_ROOT}/conf/client_nas_config.json" "${REMOTE}:${CONF_DIR}/client_config.json"
ssh "${REMOTE}" "chmod 755 ${BIN_DIR}/${BINARY_NAME}.new && mv -f ${BIN_DIR}/${BINARY_NAME}.new ${BIN_DIR}/${BINARY_NAME}"
ssh "${REMOTE}" "chmod 600 ${CONF_DIR}/client_config.json"

SERVICE_USER="$(ssh "${REMOTE}" "systemctl show -p User --value ${SERVICE_NAME}")"
if [[ -n "${SERVICE_USER}" ]]; then
    ssh "${REMOTE}" "chown ${SERVICE_USER}:${SERVICE_USER} ${BIN_DIR}/${BINARY_NAME} ${CONF_DIR}/client_config.json ${LOG_DIR}"
fi

ssh "${REMOTE}" "systemctl stop ${SERVICE_NAME}"
ssh "${REMOTE}" "systemctl restart ${SERVICE_NAME}"
ssh "${REMOTE}" "systemctl is-active --quiet ${SERVICE_NAME}"
echo "Deployed ${BINARY_NAME} to ${REMOTE}:${BIN_DIR}/${BINARY_NAME}"
