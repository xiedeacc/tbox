#!/bin/bash

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# Configuration
WORKSPACE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEB_DIR="${WORKSPACE_ROOT}/src/web"
BUILD_DIR="${WEB_DIR}/dist"

# Remote deployment configuration
REMOTE_HOST="13.215.176.217"
SSH_KEY="/home/ubuntu/.ssh/id_ed25519"
REMOTE_USER="ubuntu"
REMOTE_WEB_DIR="/data/www/admin"

echo -e "${GREEN}Starting web UI deployment to ${REMOTE_HOST}...${NC}"

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
    scp "${SSH_OPTS[@]}" "$@" "${REMOTE_USER}@${REMOTE_HOST}:$1"
}

# Test SSH connection
print_status "Testing SSH connection to ${REMOTE_HOST}..."
if ! ssh_exec "echo 'SSH connection successful'"; then
    print_error "Failed to connect to remote host"
    exit 1
fi
print_success "SSH connection verified"

# Check if pnpm is installed
print_status "Checking for pnpm..."
if ! command -v pnpm &> /dev/null; then
    print_status "pnpm not found, checking for npm..."
    if ! command -v npm &> /dev/null; then
        print_error "Neither pnpm nor npm is installed. Please install Node.js and pnpm."
        exit 1
    fi
    print_status "Using npm instead of pnpm"
    BUILD_CMD="npm"
else
    print_status "Using pnpm"
    BUILD_CMD="pnpm"
fi

# Install dependencies if needed
print_status "Installing dependencies..."
cd "${WEB_DIR}"
if [[ "$BUILD_CMD" == "pnpm" ]]; then
    if ! pnpm install; then
        print_error "Failed to install dependencies"
        exit 1
    fi
else
    if ! npm install; then
        print_error "Failed to install dependencies"
        exit 1
    fi
fi
print_success "Dependencies installed"

# Build the web application
print_status "Building web application..."
if [[ "$BUILD_CMD" == "pnpm" ]]; then
    if ! pnpm run build; then
        print_error "Failed to build web application"
        exit 1
    fi
else
    if ! npm run build; then
        print_error "Failed to build web application"
        exit 1
    fi
fi
print_success "Web application built successfully"

# Verify build output exists
if [[ ! -d "${BUILD_DIR}" ]]; then
    print_error "Build directory ${BUILD_DIR} does not exist"
    exit 1
fi

# Create remote directory if it doesn't exist
print_status "Creating remote web directory..."
ssh_exec "sudo mkdir -p ${REMOTE_WEB_DIR}"
ssh_exec "sudo chown -R ${REMOTE_USER}:${REMOTE_USER} ${REMOTE_WEB_DIR}"
print_success "Remote directory ready"

# Backup existing deployment (optional)
print_status "Backing up existing deployment..."
ssh_exec "if [ -d ${REMOTE_WEB_DIR} ] && [ \"\$(ls -A ${REMOTE_WEB_DIR})\" ]; then sudo cp -r ${REMOTE_WEB_DIR} ${REMOTE_WEB_DIR}.backup.\$(date +%Y%m%d_%H%M%S); fi" || true
print_success "Backup completed"

# Clean old web directory to ensure fresh deployment
print_status "Cleaning old web directory..."
ssh_exec "sudo rm -rf ${REMOTE_WEB_DIR}/*"
ssh_exec "sudo mkdir -p ${REMOTE_WEB_DIR}"
print_success "Old files removed"

# Deploy the built files
print_status "Deploying built files to remote host..."
# Use rsync if available, otherwise use scp
if command -v rsync &> /dev/null; then
    print_status "Using rsync for deployment..."
    if [[ -f "${SSH_KEY}" ]]; then
        rsync -avz --delete -e "ssh -i ${SSH_KEY} -o StrictHostKeyChecking=no" "${BUILD_DIR}/" "${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_WEB_DIR}/"
    else
        rsync -avz --delete -e "ssh -o StrictHostKeyChecking=no" "${BUILD_DIR}/" "${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_WEB_DIR}/"
    fi
else
    print_status "Using scp for deployment..."
    scp "${SSH_OPTS[@]}" -r "${BUILD_DIR}"/* "${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_WEB_DIR}/"
fi
print_success "Files deployed successfully"

# Set proper permissions
print_status "Setting permissions on remote host..."
ssh_exec "sudo chown -R www-data:www-data ${REMOTE_WEB_DIR}"
ssh_exec "sudo find ${REMOTE_WEB_DIR} -type f -exec chmod 644 {} \;"
ssh_exec "sudo find ${REMOTE_WEB_DIR} -type d -exec chmod 755 {} \;"
print_success "Permissions set"

# Test nginx configuration and reload
print_status "Reloading nginx configuration..."
if ssh_exec "sudo nginx -t"; then
    ssh_exec "sudo systemctl reload nginx"
    print_success "Nginx reloaded successfully"
else
    print_error "Nginx configuration test failed"
    exit 1
fi

echo
print_success "Web UI deployment completed successfully!"
print_status "Remote host: ${REMOTE_HOST}"
print_status "Web directory: ${REMOTE_WEB_DIR}"
print_status "The web UI should now be accessible at: http://${REMOTE_HOST}/admin/"
echo
print_status "Useful commands:"
echo "  - View nginx logs: ssh ${SSH_OPTS[*]} ${REMOTE_USER}@${REMOTE_HOST} 'sudo tail -f /var/log/nginx/access.log'"
echo "  - Check nginx status: ssh ${SSH_OPTS[*]} ${REMOTE_USER}@${REMOTE_HOST} 'sudo systemctl status nginx'"
echo "  - Reload nginx: ssh ${SSH_OPTS[*]} ${REMOTE_USER}@${REMOTE_HOST} 'sudo systemctl reload nginx'"
