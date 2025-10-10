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

# Build client binary locally (as current user, before sudo)
if [[ $EUID -ne 0 ]]; then
    print_status "Building client binary as current user..."
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
    TEMP_BINARY="/tmp/tbox_client_$$"
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
    
    # Now re-run as root for deployment
    print_status "Re-running as root for deployment..."
    exec sudo -E TEMP_BINARY=${TEMP_BINARY} "$0" "$@"
fi

# From here on, we're running as root
print_status "Running deployment as root..."

# Stop existing service if running
print_status "Stopping existing service if running..."
if systemctl is-active --quiet ${SERVICE_NAME}; then
    systemctl stop ${SERVICE_NAME}
    print_status "Stopped ${SERVICE_NAME} service"
fi

# TEMP_BINARY was set in the non-root section and passed via environment
if [ -z "$TEMP_BINARY" ] || [ ! -f "$TEMP_BINARY" ]; then
    print_error "Stripped binary not found at: ${TEMP_BINARY}"
    exit 1
fi
print_status "Using stripped binary: ${TEMP_BINARY}"

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
cp ${TEMP_BINARY} ${BIN_DIR}/${BINARY_NAME}
chmod +x ${BIN_DIR}/${BINARY_NAME}
print_success "Binary installed"

# Clean up temporary binary
rm -f ${TEMP_BINARY}

# Fetch SSL certificate chain from server
print_status "Fetching SSL certificate chain from ip.xiedeacc.com..."
CERT_FILE="${CONF_DIR}/xiedeacc.com.ca.cer"
if echo | openssl s_client -connect ip.xiedeacc.com:443 -servername ip.xiedeacc.com -showcerts 2>/dev/null | awk '/BEGIN CERTIFICATE/,/END CERTIFICATE/' > "${CERT_FILE}" 2>/dev/null; then
    if [ -s "${CERT_FILE}" ]; then
        cert_size=$(stat -c%s "${CERT_FILE}")
        print_success "SSL certificate chain fetched and saved to ${CERT_FILE} (${cert_size} bytes)"
        # Verify the certificate chain contains at least one valid certificate
        if openssl x509 -in "${CERT_FILE}" -noout -text >/dev/null 2>&1; then
            print_success "Certificate chain validated successfully"
        else
            print_error "Certificate chain validation failed"
            rm -f "${CERT_FILE}"
            exit 1
        fi
    else
        print_error "Failed to fetch certificate chain - empty file"
        rm -f "${CERT_FILE}"
        exit 1
    fi
else
    print_error "Failed to fetch SSL certificate chain from ip.xiedeacc.com"
    exit 1
fi

# Copy configuration file
print_status "Installing configuration file..."
cp conf/client_config.json ${CONF_DIR}/client_config.json
print_success "Configuration file installed"

# Set ownership and permissions
print_status "Setting ownership and permissions..."
chown -R ${SERVICE_USER}:${SERVICE_USER} ${INSTALL_DIR}
chmod 755 ${BIN_DIR}/${BINARY_NAME}
chmod 644 ${CONF_DIR}/client_config.json
chmod 644 ${CONF_DIR}/xiedeacc.com.ca.cer
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

# Wait and check service status
print_status "Waiting for service to initialize..."
sleep 3

if ! systemctl is-active --quiet ${SERVICE_NAME}; then
    print_error "Service failed to start properly"
    journalctl -u ${SERVICE_NAME} --no-pager -n 30
    exit 1
fi

print_success "Service is running"

# Check logs for successful login and report
print_status "Checking for successful login..."
RETRY_COUNT=0
MAX_RETRIES=10

while [ $RETRY_COUNT -lt $MAX_RETRIES ]; do
    if journalctl -u ${SERVICE_NAME} --no-pager -n 100 | grep -q "Successfully logged in"; then
        print_success "Client successfully logged in!"
        break
    fi
    sleep 2
    RETRY_COUNT=$((RETRY_COUNT + 1))
    print_status "Waiting for login... (attempt $((RETRY_COUNT))/${MAX_RETRIES})"
done

if [ $RETRY_COUNT -eq $MAX_RETRIES ]; then
    print_error "Client did not log in within expected time"
    echo
    echo "Recent logs:"
    journalctl -u ${SERVICE_NAME} --no-pager -n 30
    exit 1
fi

# Check logs for successful report
print_status "Checking for successful IP report..."
RETRY_COUNT=0
MAX_RETRIES=15

while [ $RETRY_COUNT -lt $MAX_RETRIES ]; do
    if journalctl -u ${SERVICE_NAME} --no-pager -n 100 | grep -q "Successfully reported client IP"; then
        print_success "Client successfully reported IP to server!"
        break
    fi
    sleep 2
    RETRY_COUNT=$((RETRY_COUNT + 1))
    print_status "Waiting for IP report... (attempt $((RETRY_COUNT))/${MAX_RETRIES})"
done

if [ $RETRY_COUNT -eq $MAX_RETRIES ]; then
    print_error "Client did not report IP within expected time"
    echo
    echo "Recent logs:"
    journalctl -u ${SERVICE_NAME} --no-pager -n 30
    exit 1
fi

echo
print_success "=== Deployment completed successfully! ==="
print_success "Client is running and successfully reporting to server"
echo
print_status "Binary location: ${BIN_DIR}/${BINARY_NAME}"
print_status "Config location: ${CONF_DIR}/client_config.json"
print_status "Certificate location: ${CONF_DIR}/xiedeacc.com.ca.cer"
print_status "Log directory: ${LOG_DIR}"
print_status "Service name: ${SERVICE_NAME}"
echo
print_status "Useful commands:"
echo "  - Check status: sudo systemctl status ${SERVICE_NAME}"
echo "  - View logs: sudo journalctl -u ${SERVICE_NAME} -f"
echo "  - Stop service: sudo systemctl stop ${SERVICE_NAME}"
echo "  - Start service: sudo systemctl start ${SERVICE_NAME}"
echo "  - Restart service: sudo systemctl restart ${SERVICE_NAME}"
echo
echo "Current service status:"
systemctl status ${SERVICE_NAME} --no-pager -l
echo
echo "Recent logs:"
journalctl -u ${SERVICE_NAME} --no-pager -n 20

