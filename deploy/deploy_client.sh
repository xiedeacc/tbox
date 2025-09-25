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

echo -e "${GREEN}Starting local deployment of tbox_client...${NC}"

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

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   print_error "This script must be run as root (use sudo)"
   exit 1
fi

# Stop existing service if running
print_status "Stopping existing service if running..."
if systemctl is-active --quiet ${SERVICE_NAME}; then
    systemctl stop ${SERVICE_NAME}
    print_status "Stopped ${SERVICE_NAME} service"
fi

# Build all targets locally
print_status "Building all targets locally..."
cd "${WORKSPACE_ROOT}"
if ! bazel build //...; then
    print_error "Failed to build all targets"
    exit 1
fi
print_success "All targets built successfully"

# Create service user if it doesn't exist
if ! id "${SERVICE_USER}" &>/dev/null; then
    print_status "Creating service user: ${SERVICE_USER}"
    useradd --system --shell /bin/false --home-dir ${INSTALL_DIR} --create-home ${SERVICE_USER}
    print_success "Created user: ${SERVICE_USER}"
else
    print_status "User ${SERVICE_USER} already exists"
fi

# Create installation directories
print_status "Creating installation directories..."
mkdir -p ${BIN_DIR}
mkdir -p ${CONF_DIR}
mkdir -p ${LOG_DIR}

# Copy the binary
print_status "Installing binary to ${BIN_DIR}/${BINARY_NAME}..."
cp bazel-bin/src/client/tbox_client ${BIN_DIR}/${BINARY_NAME}
chmod +x ${BIN_DIR}/${BINARY_NAME}
print_success "Binary installed"

# Copy configuration file
print_status "Installing configuration file..."
cp conf/client_config.json ${CONF_DIR}/client_config.json
print_success "Configuration file installed"

# Set ownership and permissions
print_status "Setting ownership and permissions..."
chown -R ${SERVICE_USER}:${SERVICE_USER} ${INSTALL_DIR}
chmod 755 ${BIN_DIR}/${BINARY_NAME}
chmod 644 ${CONF_DIR}/client_config.json
chmod 755 ${LOG_DIR}
print_success "Permissions set"

# Install systemd service file
print_status "Installing systemd service file..."
cp deploy/tbox_client.service /etc/systemd/system/
print_success "Service file installed"

# Reload systemd and enable service
print_status "Reloading systemd and enabling service..."
systemctl daemon-reload
systemctl enable ${SERVICE_NAME}
print_success "Service enabled"

# Start the service
print_status "Starting ${SERVICE_NAME} service..."
if systemctl start ${SERVICE_NAME}; then
    print_success "Service started successfully"
else
    print_error "Failed to start service"
    journalctl -u ${SERVICE_NAME} --no-pager -n 20
    exit 1
fi

# Check service status
sleep 2
if systemctl is-active --quiet ${SERVICE_NAME}; then
    print_success "Service is running properly"
    print_status "Deployment completed successfully!"
    echo
    echo "Service status:"
    systemctl status ${SERVICE_NAME} --no-pager -l
else
    print_error "Service failed to start properly"
    journalctl -u ${SERVICE_NAME} --no-pager -n 20
    exit 1
fi

echo
print_success "Local deployment completed!"
print_status "Binary location: ${BIN_DIR}/${BINARY_NAME}"
print_status "Config location: ${CONF_DIR}/client_config.json"
print_status "Log directory: ${LOG_DIR}"
print_status "Service name: ${SERVICE_NAME}"
echo
print_status "Useful commands:"
echo "  - Check status: sudo systemctl status ${SERVICE_NAME}"
echo "  - View logs: sudo journalctl -u ${SERVICE_NAME} -f"
echo "  - Stop service: sudo systemctl stop ${SERVICE_NAME}"
echo "  - Start service: sudo systemctl start ${SERVICE_NAME}"
echo "  - Restart service: sudo systemctl restart ${SERVICE_NAME}"
