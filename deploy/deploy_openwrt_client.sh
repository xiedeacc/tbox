#!/bin/bash

set -euo pipefail

SERVICE_NAME="tbox_client"
BINARY_NAME="tbox_client"
REMOTE="root@openwrt"
INSTALL_DIR="/usr/local/tbox"
BIN_DIR="${INSTALL_DIR}/bin"
CONF_DIR="${INSTALL_DIR}/conf"
DATA_DIR="${INSTALL_DIR}/data"
LOG_DIR="${INSTALL_DIR}/logs"
WORKSPACE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOCAL_BINARY="${WORKSPACE_ROOT}/bazel-bin/src/client/${BINARY_NAME}"

if [[ ! -x "${LOCAL_BINARY}" ]]; then
    echo "Missing ${LOCAL_BINARY}; build --config=clang_aarch64_linux_musl //src/client:tbox_client before deploying." >&2
    exit 1
fi

ssh "${REMOTE}" "mkdir -p ${BIN_DIR} ${CONF_DIR} ${DATA_DIR} ${LOG_DIR}"
ssh "${REMOTE}" "/etc/init.d/${SERVICE_NAME} stop 2>/dev/null || true"

TEMP_BINARY="$(mktemp /tmp/tbox_client.XXXXXX)"
trap 'rm -f "${TEMP_BINARY}"' EXIT
cp "${LOCAL_BINARY}" "${TEMP_BINARY}"
if command -v llvm-strip >/dev/null 2>&1; then
    llvm-strip "${TEMP_BINARY}"
fi

scp "${TEMP_BINARY}" "${REMOTE}:${BIN_DIR}/${BINARY_NAME}.new"
scp "${WORKSPACE_ROOT}/conf/client_openwrt_config.json" "${REMOTE}:${CONF_DIR}/client_config.json"
scp "${WORKSPACE_ROOT}/deploy/tbox_client.openwrt.init" "${REMOTE}:/etc/init.d/${SERVICE_NAME}"
ssh "${REMOTE}" "chmod 755 ${BIN_DIR}/${BINARY_NAME}.new /etc/init.d/${SERVICE_NAME} && mv -f ${BIN_DIR}/${BINARY_NAME}.new ${BIN_DIR}/${BINARY_NAME} && chmod 644 ${CONF_DIR}/client_config.json"

if ! ssh "${REMOTE}" "test -s ${CONF_DIR}/xiedeacc.com.ca.cer"; then
    ssh "${REMOTE}" "echo | openssl s_client -connect ip.xiedeacc.com:443 -servername ip.xiedeacc.com -showcerts 2>/dev/null | awk '/BEGIN CERTIFICATE/,/END CERTIFICATE/' > ${CONF_DIR}/xiedeacc.com.ca.cer"
fi

ssh "${REMOTE}" "/etc/init.d/${SERVICE_NAME} enable && /etc/init.d/${SERVICE_NAME} restart"
sleep 2
ssh "${REMOTE}" "/etc/init.d/${SERVICE_NAME} running"
echo "Deployed ${BINARY_NAME} to ${REMOTE}:${BIN_DIR}/${BINARY_NAME}"
