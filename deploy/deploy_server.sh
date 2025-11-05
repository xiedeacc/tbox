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
SERVICE_USER="ubuntu"
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

# SCP file helper function
scp_file() {
    local src="$1"
    local dest="$2"
    scp "${SSH_OPTS[@]}" "$src" "${REMOTE_USER}@${REMOTE_HOST}:$dest"
}


# Test SSH connection
print_status "Testing SSH connection to ${REMOTE_HOST}..."
if ! ssh_exec "echo 'SSH connection successful'"; then
    print_error "Failed to connect to remote host"
    exit 1
fi
print_success "SSH connection verified"


# NOTE: Service will be stopped later right before swapping the binary to minimize downtime

# Build all targets locally for ARM64
print_status "Building all targets locally for ARM64 (cpu_model=neoverse-11)..."
cd "${WORKSPACE_ROOT}"
if ! bazel build --define=cpu_model=neoverse-11 --config=clang_aarch64_linux_gnu //src/server:tbox_server; then
    print_error "Failed to build all targets"
    exit 1
fi
print_success "All targets built successfully"

# Get the cross-compilation strip tool from Bazel toolchain
get_bazel_strip_tool() {
    # Query Bazel for the toolchain path used in cross-compilation
    local toolchain_info
    toolchain_info=$(bazel query --config=clang_aarch64_linux_gnu \
        'kind("cc_toolchain", deps(@cc_toolchain_config_aarch64_linux_generic_glibc_clang//:toolchain-aarch64_linux_generic_glibc_clang))' \
        2>/dev/null | head -1)
    
    if [[ -n "$toolchain_info" ]]; then
        # Extract the external repository path
        local repo_path
        repo_path=$(bazel info --config=clang_aarch64_linux_gnu output_base 2>/dev/null)
        if [[ -n "$repo_path" ]]; then
            local strip_path="${repo_path}/external/cc_toolchain_repo_aarch64_linux_generic_glibc_clang/bin/llvm-strip"
            if [[ -x "$strip_path" ]]; then
                echo "$strip_path"
                return 0
            fi
        fi
    fi
    
    # Fallback: try to find the toolchain in the current Bazel cache
    local bazel_base
    bazel_base=$(bazel info output_base 2>/dev/null || echo "$HOME/.cache/bazel/_bazel_$(whoami)")
    
    # Look for the aarch64 clang toolchain strip
    local strip_candidates=(
        "${bazel_base}/external/cc_toolchain_repo_aarch64_linux_generic_glibc_clang/bin/llvm-strip"
        "${bazel_base}/*/external/cc_toolchain_repo_aarch64_linux_generic_glibc_clang/bin/llvm-strip"
    )
    
    for strip_path in "${strip_candidates[@]}"; do
        # Handle glob pattern
        if [[ "$strip_path" == *"*"* ]]; then
            for expanded_path in $strip_path; do
                if [[ -x "$expanded_path" ]]; then
                    echo "$expanded_path"
                    return 0
                fi
            done
        else
            if [[ -x "$strip_path" ]]; then
                echo "$strip_path"
                return 0
            fi
        fi
    done
    
    # Final fallback: try system llvm-strip or regular strip
    if command -v llvm-strip >/dev/null 2>&1; then
        echo "llvm-strip"
    elif command -v strip >/dev/null 2>&1; then
        echo "strip"
    else
        echo "strip"  # Will fail, but at least we tried
    fi
}

# Strip the binary to reduce size
print_status "Stripping binary to reduce size..."
STRIP_TOOL=$(get_bazel_strip_tool)
print_status "Using strip tool: $STRIP_TOOL"

TEMP_BINARY="${HOME}/tbox_server_stripped_temp"
cp bazel-bin/src/server/tbox_server "$TEMP_BINARY"
chmod u+w "$TEMP_BINARY"

if "$STRIP_TOOL" "$TEMP_BINARY"; then
    print_success "Binary stripped successfully with $STRIP_TOOL"
    
    # Show size comparison
    original_size=$(stat -c%s bazel-bin/src/server/tbox_server)
    stripped_size=$(stat -c%s "$TEMP_BINARY")
    reduction=$((original_size - stripped_size))
    print_status "Size reduction: $(numfmt --to=iec $original_size) → $(numfmt --to=iec $stripped_size) (saved $(numfmt --to=iec $reduction))"
else
    print_error "Failed to strip binary with $STRIP_TOOL, using original"
    # Copy original binary if stripping failed
    cp bazel-bin/src/server/tbox_server "$TEMP_BINARY"
fi

# Verify service user exists on remote host
print_status "Verifying service user ${SERVICE_USER} exists on remote host..."
if ssh_exec "sudo id ${SERVICE_USER} &>/dev/null"; then
    print_success "Service user ${SERVICE_USER} verified on remote host"
else
    print_error "Service user ${SERVICE_USER} does not exist on remote host"
    exit 1
fi

# Create installation directories on remote host
print_status "Creating installation directories on remote host..."
ssh_exec "sudo mkdir -p ${BIN_DIR} ${CONF_DIR} ${LOG_DIR} ${DATA_DIR}"
print_success "Directories created"

# Copy the binary to a temporary location on remote host (swap later)
print_status "Copying binary to remote host temporary location..."
scp_copy "$TEMP_BINARY" "${REMOTE_USER}@${REMOTE_HOST}:/tmp/${BINARY_NAME}"
print_success "Binary uploaded to /tmp on remote host"

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

# Stop service and swap binary to minimize downtime
print_status "Stopping ${SERVICE_NAME} on remote host before swapping binary..."
ssh_exec "sudo systemctl stop ${SERVICE_NAME} 2>/dev/null || true"
print_status "Replacing binary on remote host..."
ssh_exec "sudo mv /tmp/${BINARY_NAME} ${BIN_DIR}/${BINARY_NAME}"
ssh_exec "sudo chmod +x ${BIN_DIR}/${BINARY_NAME}"
print_success "Binary replaced on remote host"

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
print_status "Restarting ${SERVICE_NAME} service on remote host..."
if ssh_exec "sudo systemctl restart ${SERVICE_NAME}"; then
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

# Deploy nginx if needed
deploy_nginx_if_needed() {
    print_status "Checking nginx installation and configuration..."
    
    # Check if nginx-full is installed
    if ! ssh_exec "dpkg -l | grep -q nginx-full"; then
        print_status "nginx-full not installed, running init_aws.sh..."
        scp_file "${WORKSPACE_ROOT}/deploy/init_aws.sh" "/tmp/init_aws.sh"
        ssh_exec "chmod +x /tmp/init_aws.sh"
        ssh_exec "sudo /tmp/init_aws.sh"
        
        # Deploy nginx.conf after initialization
        print_status "Deploying nginx configuration..."
        scp_file "${WORKSPACE_ROOT}/deploy/nginx.conf" "/tmp/nginx.conf"
        ssh_exec "sudo mv /tmp/nginx.conf /etc/nginx/nginx.conf"
        ssh_exec "sudo chmod 644 /etc/nginx/nginx.conf"
        ssh_exec "sudo nginx -t"  # Test configuration
        ssh_exec "sudo systemctl reload nginx"
        print_success "nginx configuration deployed and reloaded"
        
        print_success "Server initialization completed"
        return 0
    fi
    
    print_status "nginx-full is installed, checking configuration..."
    
    # Check if nginx.conf contains required server sections
    local has_ip_server=false
    local has_blog_server=false
    
    if ssh_exec "grep -q 'server_name.*ip\.xiedeacc\.com' /etc/nginx/nginx.conf"; then
        has_ip_server=true
    fi
    
    if ssh_exec "grep -q 'server_name.*blog\.xiedeacc\.com' /etc/nginx/nginx.conf"; then
        has_blog_server=true
    fi
    
    if [[ "$has_ip_server" == true && "$has_blog_server" == true ]]; then
        print_success "nginx configuration contains required server sections"
        return 0
    else
        print_status "nginx configuration missing required server sections, running init_aws.sh..."
        scp_file "${WORKSPACE_ROOT}/deploy/init_aws.sh" "/tmp/init_aws.sh"
        ssh_exec "chmod +x /tmp/init_aws.sh"
        ssh_exec "sudo /tmp/init_aws.sh"
        
        # Deploy nginx.conf after initialization
        print_status "Deploying nginx configuration..."
        scp_file "${WORKSPACE_ROOT}/deploy/nginx.conf" "/tmp/nginx.conf"
        ssh_exec "sudo mv /tmp/nginx.conf /etc/nginx/nginx.conf"
        ssh_exec "sudo chmod 644 /etc/nginx/nginx.conf"
        ssh_exec "sudo nginx -t"  # Test configuration
        ssh_exec "sudo systemctl reload nginx"
        print_success "nginx configuration deployed and reloaded"
        
        print_success "Server initialization completed"
        return 0
    fi
}

# Deploy nginx if needed
deploy_nginx_if_needed

# Deploy shadowsocks-server if service doesn't exist
deploy_shadowsocks_if_needed() {
    print_status "Checking if shadowsocks-server service exists..."
    if ssh_exec "sudo systemctl list-unit-files shadowsocks-server.service" | grep -q "shadowsocks-server.service"; then
        print_status "shadowsocks-server service already exists, skipping deployment"
        return 0
    fi
    
    print_status "shadowsocks-server service not found, deploying..."
    
    # Create shadowsocks directories
    print_status "Creating shadowsocks directories on remote host..."
    ssh_exec "sudo mkdir -p /etc/shadowsocks-rust"
    ssh_exec "sudo mkdir -p /usr/local/bin"
    
    # Deploy shadowsocks binaries
    print_status "Deploying shadowsocks binaries..."
    scp_file "${WORKSPACE_ROOT}/bin/ssserver" "/tmp/ssserver"
    scp_file "${WORKSPACE_ROOT}/bin/v2ray-plugin_linux_arm64" "/tmp/v2ray-plugin_linux_arm64"
    scp_file "${WORKSPACE_ROOT}/bin/xray-plugin_linux_arm64" "/tmp/xray-plugin_linux_arm64"
    
    # Move binaries to final location and set permissions
    ssh_exec "sudo mv /tmp/ssserver /usr/local/bin/ssserver"
    ssh_exec "sudo mv /tmp/v2ray-plugin_linux_arm64 /usr/local/bin/v2ray-plugin_linux_arm64"
    ssh_exec "sudo mv /tmp/xray-plugin_linux_arm64 /usr/local/bin/xray-plugin_linux_arm64"
    ssh_exec "sudo chmod 755 /usr/local/bin/ssserver"
    ssh_exec "sudo chmod 755 /usr/local/bin/v2ray-plugin_linux_arm64"
    ssh_exec "sudo chmod 755 /usr/local/bin/xray-plugin_linux_arm64"
    print_success "Shadowsocks binaries deployed"
    
    # Deploy shadowsocks configuration
    print_status "Deploying shadowsocks configuration..."
    scp_file "${WORKSPACE_ROOT}/deploy/shadowsocks-server.json" "/tmp/shadowsocks-server.json"
    ssh_exec "sudo mv /tmp/shadowsocks-server.json /etc/shadowsocks-rust/shadowsocks-server.json"
    ssh_exec "sudo chmod 644 /etc/shadowsocks-rust/shadowsocks-server.json"
    print_success "Shadowsocks configuration deployed"
    
    # Deploy shadowsocks service file
    print_status "Deploying shadowsocks service file..."
    scp_file "${WORKSPACE_ROOT}/deploy/shadowsocks-server.service" "/tmp/shadowsocks-server.service"
    ssh_exec "sudo mv /tmp/shadowsocks-server.service /etc/systemd/system/shadowsocks-server.service"
    ssh_exec "sudo chmod 644 /etc/systemd/system/shadowsocks-server.service"
    print_success "Shadowsocks service file deployed"
    
    # Enable and start shadowsocks service
    print_status "Enabling and starting shadowsocks service..."
    ssh_exec "sudo systemctl daemon-reload"
    ssh_exec "sudo systemctl enable shadowsocks-server"
    if ssh_exec "sudo systemctl start shadowsocks-server"; then
        print_success "Shadowsocks service started successfully"
        
        # Check service status
        sleep 2
        if ssh_exec "sudo systemctl is-active --quiet shadowsocks-server"; then
            print_success "Shadowsocks service is running properly"
        else
            print_error "Shadowsocks service failed to start properly"
            ssh_exec "sudo journalctl -u shadowsocks-server --no-pager -n 10"
        fi
    else
        print_error "Failed to start shadowsocks service"
        ssh_exec "sudo journalctl -u shadowsocks-server --no-pager -n 10"
    fi
}

# Deploy shadowsocks if needed
deploy_shadowsocks_if_needed

# Deploy vlmcsd-server if service doesn't exist
deploy_vlmcsd_if_needed() {
    print_status "Checking if vlmcsd service exists..."
    if ssh_exec "sudo systemctl list-unit-files vlmcsd.service" | grep -q "vlmcsd.service"; then
        print_status "vlmcsd service already exists, skipping deployment"
        return 0
    fi
    
    print_status "vlmcsd service not found, deploying..."
    
    # Create vlmcsd directories
    print_status "Creating vlmcsd directories on remote host..."
    ssh_exec "sudo mkdir -p /usr/local/bin"
    ssh_exec "sudo mkdir -p /var/run"
    
    # Deploy vlmcsd binary
    print_status "Deploying vlmcsd binary..."
    scp_file "${WORKSPACE_ROOT}/bin/vlmcsd-armv7el-uclibc-static" "/tmp/vlmcsd"
    
    # Move binary to final location and set permissions
    ssh_exec "sudo mv /tmp/vlmcsd /usr/local/bin/vlmcsd"
    ssh_exec "sudo chmod 755 /usr/local/bin/vlmcsd"
    print_success "vlmcsd binary deployed"
    
    # Deploy vlmcsd service file
    print_status "Deploying vlmcsd service file..."
    scp_file "${WORKSPACE_ROOT}/deploy/vlmcsd.service" "/tmp/vlmcsd.service"
    ssh_exec "sudo mv /tmp/vlmcsd.service /etc/systemd/system/vlmcsd.service"
    ssh_exec "sudo chmod 644 /etc/systemd/system/vlmcsd.service"
    print_success "vlmcsd service file deployed"
    
    # Enable and start vlmcsd service
    print_status "Enabling and starting vlmcsd service..."
    ssh_exec "sudo systemctl daemon-reload"
    ssh_exec "sudo systemctl enable vlmcsd"
    if ssh_exec "sudo systemctl start vlmcsd"; then
        print_success "vlmcsd service started successfully"
        
        # Check service status
        sleep 2
        if ssh_exec "sudo systemctl is-active --quiet vlmcsd"; then
            print_success "vlmcsd service is running properly"
        else
            print_error "vlmcsd service failed to start properly"
            ssh_exec "sudo journalctl -u vlmcsd --no-pager -n 10"
        fi
    else
        print_error "Failed to start vlmcsd service"
        ssh_exec "sudo journalctl -u vlmcsd --no-pager -n 10"
    fi
}

# Deploy vlmcsd if needed
deploy_vlmcsd_if_needed

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
echo "  - Check tbox status: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo systemctl status ${SERVICE_NAME}'"
echo "  - View tbox logs: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo journalctl -u ${SERVICE_NAME} -f'"
echo "  - Stop tbox service: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo systemctl stop ${SERVICE_NAME}'"
echo "  - Start tbox service: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo systemctl start ${SERVICE_NAME}'"
echo "  - Restart tbox service: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo systemctl restart ${SERVICE_NAME}'"
echo "  - Check shadowsocks status: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo systemctl status shadowsocks-server'"
echo "  - View shadowsocks logs: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo journalctl -u shadowsocks-server -f'"
echo "  - Restart shadowsocks: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo systemctl restart shadowsocks-server'"
echo "  - Check vlmcsd status: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo systemctl status vlmcsd'"
echo "  - View vlmcsd logs: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo journalctl -u vlmcsd -f'"
echo "  - Restart vlmcsd: ssh -i ${SSH_KEY} ${REMOTE_USER}@${REMOTE_HOST} 'sudo systemctl restart vlmcsd'"
