#!/bin/bash

set -euo pipefail

SERVICE_NAME="tbox_client"
BINARY_NAME="tbox_client"
REMOTE="${REMOTE:-root@nas}"
if [[ "${REMOTE}" != *@* ]]; then
    echo "REMOTE must use the explicit user@HostAlias form (for example, root@nas)." >&2
    exit 1
fi
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
test -s ~/.ssh/id_ed25519
test -s ~/.ssh/id_ed25519.pub
ssh "${REMOTE}" "mkdir -p ${BIN_DIR} ${LOG_DIR} /root/.ssh && chmod 700 /root/.ssh"

TEMP_BINARY="$(mktemp /tmp/tbox_client.XXXXXX)"
TEMP_CONFIG="$(mktemp /tmp/tbox_client_config.XXXXXX)"
trap 'rm -f "${TEMP_BINARY}" "${TEMP_CONFIG}"' EXIT
cp "${LOCAL_BINARY}" "${TEMP_BINARY}"
if command -v llvm-strip >/dev/null 2>&1; then
    llvm-strip --strip-unneeded "${TEMP_BINARY}"
else
    strip --strip-unneeded "${TEMP_BINARY}"
fi

python3 "${WORKSPACE_ROOT}/deploy/prepare_config.py" client \
    "${WORKSPACE_ROOT}/conf/client_nas_config.json" "${TEMP_CONFIG}" \
    --host nas
scp "${TEMP_BINARY}" "${REMOTE}:${BIN_DIR}/${BINARY_NAME}.new"
scp "${TEMP_CONFIG}" "${REMOTE}:${CONF_DIR}/client_config.json"
scp ~/.ssh/id_ed25519 ~/.ssh/id_ed25519.pub "${REMOTE}:/root/.ssh/"
ssh "${REMOTE}" "chmod 600 /root/.ssh/id_ed25519 && chmod 644 /root/.ssh/id_ed25519.pub"
ssh "${REMOTE}" "for ca_source in /etc/ssl/certs/ca-certificates.crt /etc/ssl/cert.pem; do if test -s \"\${ca_source}\"; then cp -f \"\${ca_source}\" ${CONF_DIR}/ca-bundle.pem && chmod 644 ${CONF_DIR}/ca-bundle.pem && exit 0; fi; done; echo 'No system CA bundle found on NAS' >&2; exit 1"
ssh "${REMOTE}" "rm -f ${CONF_DIR}/xiedeacc.com.ca.cer"
ssh "${REMOTE}" "chmod 755 ${BIN_DIR}/${BINARY_NAME}.new && mv -f ${BIN_DIR}/${BINARY_NAME}.new ${BIN_DIR}/${BINARY_NAME}"
ssh "${REMOTE}" "chmod 600 ${CONF_DIR}/client_config.json"
ssh "${REMOTE}" "mkdir -p /etc/systemd/system/${SERVICE_NAME}.service.d && printf '%s\n' '[Service]' 'ProtectHome=read-only' > /etc/systemd/system/${SERVICE_NAME}.service.d/tbox.conf && systemctl daemon-reload"

SERVICE_USER="$(ssh "${REMOTE}" "systemctl show -p User --value ${SERVICE_NAME}")"
if [[ -n "${SERVICE_USER}" ]]; then
    ssh "${REMOTE}" "chown ${SERVICE_USER}:${SERVICE_USER} ${BIN_DIR}/${BINARY_NAME} ${CONF_DIR}/client_config.json ${LOG_DIR}"
fi

ssh "${REMOTE}" "systemctl stop ${SERVICE_NAME}"
ssh "${REMOTE}" "rm -f ${LOG_DIR}/tbox_client*.log*"
ssh "${REMOTE}" "systemctl restart ${SERVICE_NAME}"
ssh "${REMOTE}" "systemctl is-active --quiet ${SERVICE_NAME}"
echo "Deployed ${BINARY_NAME} to ${REMOTE}:${BIN_DIR}/${BINARY_NAME}"
