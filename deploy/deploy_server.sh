#!/bin/bash

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# Configuration
SERVICE_NAME="tbox_server"
BINARY_NAME="tbox_server"
INSTALL_DIR="/usr/local/tbox"
BIN_DIR="${INSTALL_DIR}/bin"
CONF_DIR="${INSTALL_DIR}/conf"
LOG_DIR="${INSTALL_DIR}/log"
DATA_DIR="${INSTALL_DIR}/data"
SERVICE_USER="tbox"
WORKSPACE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Remote deployment configuration
REMOTE_HOST="13.215.176.217"
SSH_KEY="/home/ubuntu/.ssh/id_ed25519"
REMOTE_USER="ubuntu"

echo -e "${GREEN}Starting deployment of tbox to ${REMOTE_HOST}...${NC}"

# Function to print status
print_status() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

# SSH options (use key if available, otherwise rely on default identities)
if [[ -f "${SSH_KEY}" ]]; then
    SSH_OPTS=( -i "${SSH_KEY}" -o StrictHostKeyChecking=no )
    # Fix SSH key permissions if needed
    if [[ "$(stat -c %a "${SSH_KEY}")" != "600" ]]; then
        print_status "Fixing SSH key permissions..."
        chmod 600 "${SSH_KEY}"
        print_success "SSH key permissions updated to 600"
    fi
else
    SSH_OPTS=( -o StrictHostKeyChecking=no )
    print_status "SSH key ${SSH_KEY} not found; using default SSH identities"
fi

# SSH helper function
ssh_exec() {
    ssh "${SSH_OPTS[@]}" "${REMOTE_USER}@${REMOTE_HOST}" "$@"
}

# SCP helper function
scp_copy() {
    scp "${SSH_OPTS[@]}" "$@"
}

# Test SSH connection
print_status "Testing SSH connection to ${REMOTE_HOST}..."
if ! ssh_exec "echo 'SSH connection successful'"; then
    print_error "Failed to connect to remote host"
    exit 1
fi
print_success "SSH connection verified"

# Stop existing service if running on remote host
print_status "Stopping existing service on remote host if running..."
ssh_exec "sudo systemctl stop ${SERVICE_NAME} 2>/dev/null || true"

# Build all targets locally for ARM64
print_status "Building all targets locally for ARM64 (cpu_model=neoverse-11)..."
cd "${WORKSPACE_ROOT}"
if ! bazel build --define=cpu_model=neoverse-11 --config=clang_aarch64_linux_gnu //...; then
    print_error "Failed to build all targets"
    exit 1
fi
print_success "All targets built successfully"

# Strip the binary to reduce size
print_status "Stripping binary to reduce size..."
TEMP_BINARY="${HOME}/tbox_server_stripped_temp"
cp bazel-bin/src/server/tbox_server "$TEMP_BINARY"
chmod u+w "$TEMP_BINARY"
if strip "$TEMP_BINARY"; then
    print_success "Binary stripped successfully"
    
    # Show size comparison
    original_size=$(stat -c%s bazel-bin/src/server/tbox_server)
    stripped_size=$(stat -c%s "$TEMP_BINARY")
    reduction=$((original_size - stripped_size))
    print_status "Size reduction: $(numfmt --to=iec $original_size) → $(numfmt --to=iec $stripped_size) (saved $(numfmt --to=iec $reduction))"
else
    print_error "Failed to strip binary, using original"
fi

# Create service user on remote host if it doesn't exist
print_status "Creating service user on remote host if needed..."
ssh_exec "sudo id ${SERVICE_USER} &>/dev/null || sudo useradd --system --shell /bin/false --home-dir ${INSTALL_DIR} --create-home ${SERVICE_USER}"
print_success "Service user verified on remote host"

# Create installation directories on remote host
print_status "Creating installation directories on remote host..."
ssh_exec "sudo mkdir -p ${BIN_DIR} ${CONF_DIR} ${LOG_DIR} ${DATA_DIR}"
print_success "Directories created"

# Copy the stripped binary to remote host
print_status "Copying stripped binary to remote host..."
scp_copy "$TEMP_BINARY" "${REMOTE_USER}@${REMOTE_HOST}:/tmp/${BINARY_NAME}"
ssh_exec "sudo mv /tmp/${BINARY_NAME} ${BIN_DIR}/${BINARY_NAME}"
ssh_exec "sudo chmod +x ${BIN_DIR}/${BINARY_NAME}"
print_success "Binary installed on remote host"

# Copy configuration file to remote host
print_status "Copying configuration file to remote host..."
scp_copy conf/server_config.json "${REMOTE_USER}@${REMOTE_HOST}:/tmp/server_config.json"
ssh_exec "sudo mv /tmp/server_config.json ${CONF_DIR}/server_config.json"
print_success "Configuration file installed on remote host"

# Copy and install systemd service file
print_status "Installing systemd service file on remote host..."
scp_copy deploy/tbox_server.service "${REMOTE_USER}@${REMOTE_HOST}:/tmp/tbox_server.service"
ssh_exec "sudo mv /tmp/tbox_server.service /etc/systemd/system/"
print_success "Service file installed on remote host"

# Set ownership and permissions on remote host
print_status "Setting ownership and permissions on remote host..."
ssh_exec "sudo chown -R ${SERVICE_USER}:${SERVICE_USER} ${INSTALL_DIR}"
ssh_exec "sudo chmod 755 ${BIN_DIR}/${BINARY_NAME}"
ssh_exec "sudo chmod 644 ${CONF_DIR}/server_config.json"
ssh_exec "sudo chmod 755 ${LOG_DIR}"
ssh_exec "sudo chmod 755 ${DATA_DIR}"
print_success "Permissions set on remote host"

# Reload systemd and enable service on remote host
print_status "Reloading systemd and enabling service on remote host..."
ssh_exec "sudo systemctl daemon-reload"
ssh_exec "sudo systemctl enable ${SERVICE_NAME}"
print_success "Service enabled on remote host"

# Start the service on remote host
print_status "Starting ${SERVICE_NAME} service on remote host..."
if ssh_exec "sudo systemctl start ${SERVICE_NAME}"; then
    print_success "Service started successfully on remote host"
else
    print_error "Failed to start service on remote host"
    ssh_exec "sudo journalctl -u ${SERVICE_NAME} --no-pager -n 20"
    exit 1
fi

# Check service status on remote host
sleep 2
if ssh_exec "sudo systemctl is-active --quiet ${SERVICE_NAME}"; then
    print_success "Service is running properly on remote host"
    print_status "Deployment completed successfully!"
    echo
    echo "Service status on remote host:"
    ssh_exec "sudo systemctl status ${SERVICE_NAME} --no-pager -l"
else
    print_error "Service failed to start properly on remote host"
    ssh_exec "sudo journalctl -u ${SERVICE_NAME} --no-pager -n 20"
    exit 1
fi

# Clean up temporary files
print_status "Cleaning up temporary files..."
rm -f "$TEMP_BINARY"

echo
print_success "Remote deployment completed!"
print_status "Remote host: ${REMOTE_HOST}"
print_status "Binary location: ${BIN_DIR}/${BINARY_NAME}"
print_status "Config location: ${CONF_DIR}/server_config.json"
print_status "Log directory: ${LOG_DIR}"
print_status "Data directory: ${DATA_DIR}"
print_status "Service name: ${SERVICE_NAME}"
echo
print_status "Useful remote commands:"
echo "  - Check status: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo systemctl status ${SERVICE_NAME}'"
echo "  - View logs: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo journalctl -u ${SERVICE_NAME} -f'"
echo "  - Stop service: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo systemctl stop ${SERVICE_NAME}'"
echo "  - Start service: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo systemctl start ${SERVICE_NAME}'"
echo "  - Restart service: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo systemctl restart ${SERVICE_NAME}'"
