#!/bin/bash

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

SERVICE_NAME="tbox_client"
BINARY_NAME="tbox_client"
INSTALL_DIR="/usr/local/tbox"
BIN_DIR="${INSTALL_DIR}/bin"
CONF_DIR="${INSTALL_DIR}/conf"
DATA_DIR="${INSTALL_DIR}/data"
LOG_DIR="${INSTALL_DIR}/logs"
WORKSPACE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

REMOTE_HOST="192.168.2.1"
REMOTE_PORT="10022"
REMOTE_USER="root"

print_status() { echo -e "${YELLOW}[INFO]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }

SSH_OPTS=(-p "${REMOTE_PORT}" -o StrictHostKeyChecking=no)
SCP_OPTS=(-P "${REMOTE_PORT}" -o StrictHostKeyChecking=no)

ssh_exec() {
    ssh "${SSH_OPTS[@]}" "${REMOTE_USER}@${REMOTE_HOST}" "$@"
}

scp_file() {
    scp "${SCP_OPTS[@]}" "$1" "${REMOTE_USER}@${REMOTE_HOST}:$2"
}

echo -e "${GREEN}Starting deployment of tbox_client to OpenWrt (${REMOTE_HOST}:${REMOTE_PORT})...${NC}"

print_status "Testing SSH connection..."
if ! ssh_exec "echo 'SSH connection successful'"; then
    print_error "Failed to connect to OpenWrt host"
    exit 1
fi
print_success "SSH connection verified"

print_status "Stopping existing procd service if running..."
ssh_exec "/etc/init.d/${SERVICE_NAME} stop 2>/dev/null || true"

print_status "Building OpenWrt client binary..."
cd "${WORKSPACE_ROOT}"
if [[ -z "${JAVA_HOME:-}" ]]; then
    print_error "JAVA_HOME is not set. Example: export JAVA_HOME=/usr/local/java/jdk/jdk-21.0.3"
    exit 1
fi

if ! bazel build //src/client:tbox_client --config=gcc_aarch64_linux_musl; then
    print_error "Failed to build client binary"
    exit 1
fi
print_success "Client binary built successfully"

TEMP_BINARY="/tmp/tbox_client_${RANDOM}"
cp bazel-bin/src/client/tbox_client "${TEMP_BINARY}"
chmod +w "${TEMP_BINARY}"
if command -v llvm-strip >/dev/null 2>&1; then
    llvm-strip "${TEMP_BINARY}" || true
elif command -v strip >/dev/null 2>&1; then
    strip "${TEMP_BINARY}" || true
fi
print_status "Prepared binary size: $(ls -lh "${TEMP_BINARY}" | awk '{print $5}')"

print_status "Creating OpenWrt installation directories..."
ssh_exec "mkdir -p ${BIN_DIR} ${CONF_DIR} ${DATA_DIR} ${LOG_DIR}"

print_status "Installing binary..."
scp_file "${TEMP_BINARY}" "${BIN_DIR}/${BINARY_NAME}.new"
ssh_exec "chmod +x ${BIN_DIR}/${BINARY_NAME}.new && mv -f ${BIN_DIR}/${BINARY_NAME}.new ${BIN_DIR}/${BINARY_NAME}"
rm -f "${TEMP_BINARY}"
print_success "Binary installed"

print_status "Installing configuration..."
scp_file "conf/client_openwrt_config.json" "${CONF_DIR}/client_config.json"
print_success "Configuration installed"

print_status "Fetching SSL certificate chain on OpenWrt..."
ssh_exec "echo | openssl s_client -connect ip.xiedeacc.com:443 -servername ip.xiedeacc.com -showcerts 2>/dev/null | awk '/BEGIN CERTIFICATE/,/END CERTIFICATE/' > ${CONF_DIR}/xiedeacc.com.ca.cer 2>/dev/null && test -s ${CONF_DIR}/xiedeacc.com.ca.cer"
print_success "Certificate installed"

print_status "Installing procd init script..."
scp_file "deploy/tbox_client.openwrt.init" "/etc/init.d/${SERVICE_NAME}"
ssh_exec "chmod +x /etc/init.d/${SERVICE_NAME}"
print_success "procd init script installed"

print_status "Setting permissions..."
ssh_exec "chmod 755 ${BIN_DIR}/${BINARY_NAME} ${BIN_DIR} ${CONF_DIR} ${DATA_DIR} ${LOG_DIR} && chmod 644 ${CONF_DIR}/client_config.json ${CONF_DIR}/xiedeacc.com.ca.cer"
print_success "Permissions set"

print_status "Enabling and starting procd service..."
ssh_exec "/etc/init.d/${SERVICE_NAME} enable && /etc/init.d/${SERVICE_NAME} restart"

sleep 2
if ssh_exec "/etc/init.d/${SERVICE_NAME} running"; then
    print_success "Service is running"
else
    print_error "Service failed to start"
    ssh_exec "logread | tail -n 50"
    exit 1
fi

print_success "Deployment to OpenWrt completed"
print_status "Binary location: ${BIN_DIR}/${BINARY_NAME}"
print_status "Config location: ${CONF_DIR}/client_config.json"
print_status "Data directory: ${DATA_DIR}"
print_status "Log directory: ${LOG_DIR}"
print_status "Useful commands:"
echo "  ssh -p ${REMOTE_PORT} ${REMOTE_USER}@${REMOTE_HOST} '/etc/init.d/${SERVICE_NAME} status'"
echo "  ssh -p ${REMOTE_PORT} ${REMOTE_USER}@${REMOTE_HOST} 'logread -f | grep ${SERVICE_NAME}'"
