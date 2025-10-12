#!/bin/bash

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# Configuration
SERVICE_NAME="tbox_client"
BINARY_NAME="tbox_client"
INSTALL_DIR="/usr/local/tbox"
BIN_DIR="${INSTALL_DIR}/bin"
CONF_DIR="${INSTALL_DIR}/conf"
LOG_DIR="${INSTALL_DIR}/log"
SERVICE_USER="tbox"
WORKSPACE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# NAS deployment parameters (pre-configured)
REMOTE_HOST="192.168.3.100"
REMOTE_PORT="10022"
REMOTE_USER="root"

echo -e "${GREEN}Starting deployment of tbox_client to NAS (${REMOTE_HOST}:${REMOTE_PORT})...${NC}"

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

# SSH/SCP command builders
SSH_OPTS="-p ${REMOTE_PORT} -o StrictHostKeyChecking=no"
SCP_OPTS="-P ${REMOTE_PORT} -o StrictHostKeyChecking=no"
SSH_CMD="ssh ${SSH_OPTS} ${REMOTE_USER}@${REMOTE_HOST}"
SCP_CMD="scp ${SCP_OPTS}"

# Execute command on remote
execute_cmd() {
    $SSH_CMD "$1"
}

# Stop existing service if running
print_status "Stopping existing service if running..."
execute_cmd "systemctl is-active --quiet ${SERVICE_NAME} && systemctl stop ${SERVICE_NAME} && echo 'Stopped ${SERVICE_NAME} service' || true"

# Build client binary locally
print_status "Building client binary..."
cd "${WORKSPACE_ROOT}"

# Check if JAVA_HOME is set
if [ -z "$JAVA_HOME" ]; then
    print_error "JAVA_HOME is not set. Please set JAVA_HOME before running this script."
    print_status "Example: export JAVA_HOME=/usr/local/java/jdk/jdk-21.0.3"
    exit 1
fi

if ! bazel build //src/client:tbox_client; then
    print_error "Failed to build client binary"
    exit 1
fi
print_success "Client binary built successfully"

# Strip the binary to reduce size
print_status "Stripping binary to reduce size..."
TEMP_BINARY="/tmp/tbox_client_${RANDOM}"
cp bazel-bin/src/client/tbox_client ${TEMP_BINARY}
chmod +w ${TEMP_BINARY}
if strip ${TEMP_BINARY}; then
    BINARY_SIZE=$(ls -lh ${TEMP_BINARY} | awk '{print $5}')
    print_success "Binary stripped successfully (size: ${BINARY_SIZE})"
else
    print_error "Failed to strip binary"
    rm -f ${TEMP_BINARY}
    exit 1
fi

# Create service user if it doesn't exist
print_status "Creating service user if needed..."
execute_cmd "id ${SERVICE_USER} &>/dev/null || (useradd --system --shell /bin/false --home-dir ${INSTALL_DIR} --create-home ${SERVICE_USER} && echo 'Created user: ${SERVICE_USER}') || echo 'User ${SERVICE_USER} already exists'"

# Create installation directories
print_status "Creating installation directories..."
execute_cmd "mkdir -p ${BIN_DIR} ${CONF_DIR} ${LOG_DIR}"

# Copy the binary
print_status "Installing binary to ${BIN_DIR}/${BINARY_NAME}..."
$SCP_CMD ${TEMP_BINARY} ${REMOTE_USER}@${REMOTE_HOST}:${BIN_DIR}/${BINARY_NAME}
execute_cmd "chmod +x ${BIN_DIR}/${BINARY_NAME}"
print_success "Binary installed"

# Clean up temporary binary
rm -f ${TEMP_BINARY}

# Fetch SSL certificate chain from server
print_status "Fetching SSL certificate chain from ip.xiedeacc.com..."
CERT_FILE="${CONF_DIR}/xiedeacc.com.ca.cer"
execute_cmd "echo | openssl s_client -connect ip.xiedeacc.com:443 -servername ip.xiedeacc.com -showcerts 2>/dev/null | awk '/BEGIN CERTIFICATE/,/END CERTIFICATE/' > ${CERT_FILE} 2>/dev/null && test -s ${CERT_FILE} && openssl x509 -in ${CERT_FILE} -noout -text >/dev/null 2>&1 && echo 'SSL certificate fetched and validated successfully' || (echo 'Failed to fetch SSL certificate' && exit 1)"
if [ $? -eq 0 ]; then
    print_success "SSL certificate chain fetched and validated"
else
    print_error "Failed to fetch SSL certificate chain"
    exit 1
fi

# Copy configuration file
print_status "Installing configuration file..."
$SCP_CMD conf/client_nas_config.json ${REMOTE_USER}@${REMOTE_HOST}:${CONF_DIR}/client_config.json
print_success "Configuration file installed"

# Set ownership and permissions
print_status "Setting ownership and permissions..."
execute_cmd "chown -R ${SERVICE_USER}:${SERVICE_USER} ${INSTALL_DIR} && chmod 755 ${BIN_DIR}/${BINARY_NAME} && chmod 644 ${CONF_DIR}/client_config.json && chmod 644 ${CONF_DIR}/xiedeacc.com.ca.cer && chmod 755 ${LOG_DIR}"
print_success "Permissions set"

# Install systemd service file
print_status "Installing systemd service file..."
$SCP_CMD deploy/tbox_client.service ${REMOTE_USER}@${REMOTE_HOST}:/etc/systemd/system/
print_success "Service file installed"

# Reload systemd and enable service
print_status "Reloading systemd and enabling service..."
execute_cmd "systemctl daemon-reload && systemctl enable ${SERVICE_NAME}"
print_success "Service enabled"

# Start the service
print_status "Starting ${SERVICE_NAME} service..."
execute_cmd "systemctl start ${SERVICE_NAME}"
print_success "Service start command sent"

# Check service status
sleep 2
print_status "Checking service status..."
if execute_cmd "systemctl is-active --quiet ${SERVICE_NAME}"; then
    print_success "Service is running properly"
    print_status "Deployment completed successfully!"
    echo
    echo "Service status:"
    execute_cmd "systemctl status ${SERVICE_NAME} --no-pager -l"
else
    print_error "Service failed to start properly"
    execute_cmd "journalctl -u ${SERVICE_NAME} --no-pager -n 20"
    exit 1
fi

echo
print_success "Deployment to NAS (${REMOTE_HOST}) completed!"
print_status "Binary location: ${BIN_DIR}/${BINARY_NAME}"
print_status "Config location: ${CONF_DIR}/client_config.json"
print_status "Certificate location: ${CONF_DIR}/xiedeacc.com.ca.cer"
print_status "Log directory: ${LOG_DIR}"
print_status "Service name: ${SERVICE_NAME}"
echo
print_status "Useful commands (run on remote host or with ssh):"
echo "  - Check status: ssh -p ${REMOTE_PORT} ${REMOTE_USER}@${REMOTE_HOST} 'systemctl status ${SERVICE_NAME}'"
echo "  - View logs: ssh -p ${REMOTE_PORT} ${REMOTE_USER}@${REMOTE_HOST} 'journalctl -u ${SERVICE_NAME} -f'"
echo "  - Stop service: ssh -p ${REMOTE_PORT} ${REMOTE_USER}@${REMOTE_HOST} 'systemctl stop ${SERVICE_NAME}'"
echo "  - Start service: ssh -p ${REMOTE_PORT} ${REMOTE_USER}@${REMOTE_HOST} 'systemctl start ${SERVICE_NAME}'"
echo "  - Restart service: ssh -p ${REMOTE_PORT} ${REMOTE_USER}@${REMOTE_HOST} 'systemctl restart ${SERVICE_NAME}'"

