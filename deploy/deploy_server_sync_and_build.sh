#!/bin/bash

set -e

# Configuration
REMOTE_HOST="13.215.176.217"
REMOTE_USER="ubuntu"
SSH_KEY="/home/ubuntu/.ssh/id_ed25519"
LOCAL_SRC="/home/ubuntu/src/cpp/tbox"
REMOTE_BUILD_DIR="/home/ubuntu/tbox_build"
SERVICE_NAME="tbox_server"
INSTALL_DIR="/usr/local/tbox"

GREEN='\033[0;32m'
YELLOW='\033[0;33m'
RED='\033[0;31m'
NC='\033[0m'

print_status() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

ssh_exec() {
    ssh -i "${SSH_KEY}" -o StrictHostKeyChecking=no "${REMOTE_USER}@${REMOTE_HOST}" "$@"
}

print_status "Syncing source code to remote server..."
rsync -avz --delete -e "ssh -i ${SSH_KEY} -o StrictHostKeyChecking=no" \
    --exclude 'bazel-*' \
    --exclude '.git' \
    --exclude '*.swp' \
    "${LOCAL_SRC}/" "${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_BUILD_DIR}/"

print_success "Source code synced"

print_status "Building on remote server..."
ssh_exec "cd ${REMOTE_BUILD_DIR} && bazel build //src/server:tbox_server" || {
    print_error "Build failed on remote server"
    exit 1
}

print_success "Build completed on remote server"

print_status "Deploying on remote server..."
ssh_exec "sudo systemctl stop ${SERVICE_NAME} 2>/dev/null || true"
ssh_exec "sudo mkdir -p ${INSTALL_DIR}/{bin,conf,log}"
ssh_exec "sudo cp ${REMOTE_BUILD_DIR}/bazel-bin/src/server/tbox_server ${INSTALL_DIR}/bin/"
ssh_exec "sudo cp ${REMOTE_BUILD_DIR}/conf/server_config.json ${INSTALL_DIR}/conf/"
ssh_exec "sudo cp ${REMOTE_BUILD_DIR}/deploy/tbox_server.service /etc/systemd/system/"

ssh_exec "sudo id tbox &>/dev/null || sudo useradd --system --shell /bin/false --home-dir ${INSTALL_DIR} tbox"
ssh_exec "sudo chown -R tbox:tbox ${INSTALL_DIR}"
ssh_exec "sudo chmod +x ${INSTALL_DIR}/bin/tbox_server"

ssh_exec "sudo systemctl daemon-reload"
ssh_exec "sudo systemctl enable ${SERVICE_NAME}"
ssh_exec "sudo systemctl start ${SERVICE_NAME}"

sleep 3
print_status "Checking service status..."
ssh_exec "sudo systemctl status ${SERVICE_NAME} --no-pager -l | head -20"

print_success "Deployment completed!"

