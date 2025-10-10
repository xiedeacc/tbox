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

# Remote deployment parameters
REMOTE_HOST=""
REMOTE_PORT="22"
SSH_KEY=""
REMOTE_USER="root"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --remote-host)
            REMOTE_HOST="$2"
            shift 2
            ;;
        --ssh-port)
            REMOTE_PORT="$2"
            shift 2
            ;;
        --ssh-key)
            SSH_KEY="$2"
            shift 2
            ;;
        --remote-user)
            REMOTE_USER="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--remote-host HOST] [--ssh-port PORT] [--ssh-key KEY_PATH] [--remote-user USER]"
            exit 1
            ;;
    esac
done

if [ -n "$REMOTE_HOST" ]; then
    echo -e "${GREEN}Starting remote deployment of tbox_client to ${REMOTE_HOST}...${NC}"
else
    echo -e "${GREEN}Starting local deployment of tbox_client...${NC}"
fi

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
SSH_CMD=""
SCP_CMD=""
if [ -n "$REMOTE_HOST" ]; then
    # Check if it's an IPv6 address and wrap in brackets for SCP
    if [[ "$REMOTE_HOST" =~ : ]]; then
        REMOTE_HOST_SCP="[${REMOTE_HOST}]"
    else
        REMOTE_HOST_SCP="${REMOTE_HOST}"
    fi
    
    SSH_OPTS="-p ${REMOTE_PORT} -o StrictHostKeyChecking=no"
    SCP_OPTS="-P ${REMOTE_PORT} -o StrictHostKeyChecking=no"
    if [ -n "$SSH_KEY" ]; then
        SSH_OPTS="${SSH_OPTS} -i ${SSH_KEY}"
        SCP_OPTS="${SCP_OPTS} -i ${SSH_KEY}"
    fi
    SSH_CMD="ssh ${SSH_OPTS} ${REMOTE_USER}@${REMOTE_HOST}"
    SCP_CMD="scp ${SCP_OPTS}"
fi

# Execute command (local or remote)
execute_cmd() {
    if [ -n "$REMOTE_HOST" ]; then
        $SSH_CMD "$1"
    else
        eval "$1"
    fi
}

# Check if running as root (for local deployment only)
if [ -z "$REMOTE_HOST" ]; then
    if [[ $EUID -ne 0 ]]; then
       print_error "This script must be run as root for local deployment (use sudo)"
       exit 1
    fi
fi

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
if [ -n "$REMOTE_HOST" ]; then
    $SCP_CMD ${TEMP_BINARY} ${REMOTE_USER}@${REMOTE_HOST_SCP}:${BIN_DIR}/${BINARY_NAME}
else
    cp ${TEMP_BINARY} ${BIN_DIR}/${BINARY_NAME}
fi
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
if [ -n "$REMOTE_HOST" ]; then
    $SCP_CMD conf/client_config.json ${REMOTE_USER}@${REMOTE_HOST_SCP}:${CONF_DIR}/client_config.json
else
    cp conf/client_config.json ${CONF_DIR}/client_config.json
fi
print_success "Configuration file installed"

# Set ownership and permissions
print_status "Setting ownership and permissions..."
execute_cmd "chown -R ${SERVICE_USER}:${SERVICE_USER} ${INSTALL_DIR} && chmod 755 ${BIN_DIR}/${BINARY_NAME} && chmod 644 ${CONF_DIR}/client_config.json && chmod 644 ${CONF_DIR}/xiedeacc.com.ca.cer && chmod 755 ${LOG_DIR}"
print_success "Permissions set"

# Install systemd service file
print_status "Installing systemd service file..."
if [ -n "$REMOTE_HOST" ]; then
    $SCP_CMD deploy/tbox_client.service ${REMOTE_USER}@${REMOTE_HOST_SCP}:/etc/systemd/system/
else
    cp deploy/tbox_client.service /etc/systemd/system/
fi
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
if [ -n "$REMOTE_HOST" ]; then
    print_success "Remote deployment to ${REMOTE_HOST} completed!"
else
    print_success "Local deployment completed!"
fi
print_status "Binary location: ${BIN_DIR}/${BINARY_NAME}"
print_status "Config location: ${CONF_DIR}/client_config.json"
print_status "Certificate location: ${CONF_DIR}/xiedeacc.com.ca.cer"
print_status "Log directory: ${LOG_DIR}"
print_status "Service name: ${SERVICE_NAME}"
echo
if [ -n "$REMOTE_HOST" ]; then
    print_status "Useful commands (run on remote host or with ssh):"
    echo "  - Check status: ssh -p ${REMOTE_PORT} ${REMOTE_USER}@${REMOTE_HOST} 'systemctl status ${SERVICE_NAME}'"
    echo "  - View logs: ssh -p ${REMOTE_PORT} ${REMOTE_USER}@${REMOTE_HOST} 'journalctl -u ${SERVICE_NAME} -f'"
    echo "  - Stop service: ssh -p ${REMOTE_PORT} ${REMOTE_USER}@${REMOTE_HOST} 'systemctl stop ${SERVICE_NAME}'"
    echo "  - Start service: ssh -p ${REMOTE_PORT} ${REMOTE_USER}@${REMOTE_HOST} 'systemctl start ${SERVICE_NAME}'"
    echo "  - Restart service: ssh -p ${REMOTE_PORT} ${REMOTE_USER}@${REMOTE_HOST} 'systemctl restart ${SERVICE_NAME}'"
else
    print_status "Useful commands:"
    echo "  - Check status: sudo systemctl status ${SERVICE_NAME}"
    echo "  - View logs: sudo journalctl -u ${SERVICE_NAME} -f"
    echo "  - Stop service: sudo systemctl stop ${SERVICE_NAME}"
    echo "  - Start service: sudo systemctl start ${SERVICE_NAME}"
    echo "  - Restart service: sudo systemctl restart ${SERVICE_NAME}"
fi
