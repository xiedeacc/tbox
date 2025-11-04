#!/usr/bin/env bash

set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

REMOTE_HOST="13.215.176.217"
REMOTE_USER="ubuntu"
REMOTE="${REMOTE_USER}@${REMOTE_HOST}"
SSH_OPTS="-o StrictHostKeyChecking=no"

WEB_DIR="$(cd "$(dirname "$0")/../src/web" && pwd)"
DIST_DIR="${WEB_DIR}/dist"
REMOTE_TMP="/tmp/admin_dist"
REMOTE_WEB_ROOT="/data/www/admin"
BACKEND_PORT=10001

echo -e "${YELLOW}[INFO]${NC} Building web assets..."
pushd "${WEB_DIR}" >/dev/null
pnpm install --frozen-lockfile
pnpm build
popd >/dev/null

echo -e "${YELLOW}[INFO]${NC} Syncing assets to ${REMOTE_HOST}..."
ssh ${SSH_OPTS} "${REMOTE}" "rm -rf ${REMOTE_TMP} && mkdir -p ${REMOTE_TMP}"
rsync -az -e "ssh ${SSH_OPTS}" "${DIST_DIR}/" "${REMOTE}:${REMOTE_TMP}/"
ssh ${SSH_OPTS} "${REMOTE}" "sudo mkdir -p ${REMOTE_WEB_ROOT} && sudo rsync -az --delete ${REMOTE_TMP}/ ${REMOTE_WEB_ROOT}/"

echo -e "${YELLOW}[INFO]${NC} Setting ownership to www-data..."
ssh ${SSH_OPTS} "${REMOTE}" "sudo chown -R www-data:www-data ${REMOTE_WEB_ROOT}"

echo -e "${GREEN}[SUCCESS]${NC} Web deployed to http://${REMOTE_HOST}/admin/"
#!/bin/bash

set -euo pipefail

# Colors
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
RED='\033[0;31m'
NC='\033[0m'

print_info() { echo -e "${YELLOW}[INFO]${NC} $1"; }
print_ok() { echo -e "${GREEN}[OK]${NC} $1"; }
print_err() { echo -e "${RED}[ERR]${NC} $1"; }

# Paths
WORKSPACE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEB_DIR="${WORKSPACE_ROOT}/src/web"

# Remote
REMOTE_HOST="13.215.176.217"
REMOTE_USER="ubuntu"
SSH_OPTS="-o StrictHostKeyChecking=no"

# Deploy targets
REMOTE_WEB_ROOT="/data/www"
REMOTE_ADMIN_DIR="${REMOTE_WEB_ROOT}/admin"
REMOTE_TMP_DIR="/tmp/admin_dist"
BACKUP_TS="$(date +%Y%m%d-%H%M%S)"

set +e
command -v pnpm >/dev/null 2>&1 || { print_err "pnpm is required"; exit 1; }
set -e

print_info "Building web assets..."
pushd "${WEB_DIR}" >/dev/null
pnpm install --frozen-lockfile
pnpm build
popd >/dev/null
print_ok "Web build completed"

print_info "Syncing assets to remote host ${REMOTE_HOST}..."
ssh ${SSH_OPTS} ${REMOTE_USER}@${REMOTE_HOST} "rm -rf ${REMOTE_TMP_DIR} && mkdir -p ${REMOTE_TMP_DIR}"
rsync -az -e "ssh ${SSH_OPTS}" "${WEB_DIR}/dist/" "${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_TMP_DIR}/"

print_info "Publishing to ${REMOTE_ADMIN_DIR} with sudo..."
ssh ${SSH_OPTS} ${REMOTE_USER}@${REMOTE_HOST} "sudo mkdir -p ${REMOTE_ADMIN_DIR} && sudo rsync -az --delete ${REMOTE_TMP_DIR}/ ${REMOTE_ADMIN_DIR}/"
ssh ${SSH_OPTS} ${REMOTE_USER}@${REMOTE_HOST} "sudo chown -R www-data:www-data ${REMOTE_ADMIN_DIR} && sudo find ${REMOTE_ADMIN_DIR} -type d -exec chmod 755 {} \; && sudo find ${REMOTE_ADMIN_DIR} -type f -exec chmod 644 {} \;"
print_ok "Assets published and ownership set to www-data"

echo
print_ok "Deployment completed: http://${REMOTE_HOST}/admin/"

