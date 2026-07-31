#!/bin/bash

set -euo pipefail

SERVICE_LABEL="com.xiedeacc.tbox-client"
BINARY_NAME="tbox_client"
INSTALL_DIR="/usr/local/tbox"
BIN_DIR="${INSTALL_DIR}/bin"
CONF_DIR="${INSTALL_DIR}/conf"
DATA_DIR="${INSTALL_DIR}/data"
LOG_DIR="${INSTALL_DIR}/logs"
PLIST_PATH="/Library/LaunchDaemons/${SERVICE_LABEL}.plist"
OPENWRT_REMOTE="${OPENWRT_REMOTE:-root@openwrt}"
OPENWRT_CONFIG="${OPENWRT_CONFIG:-/usr/local/tbox/conf/client_config.json}"
TBOX_CLIENT_ID="${TBOX_CLIENT_ID:-home-macmini-001}"
TBOX_USER="${TBOX_USER:-tiger-macmini}"
TBOX_SERVER_ADDR="${TBOX_SERVER_ADDR:-https://ip.xiedeacc.com}"
TBOX_GRPC_PORT="${TBOX_GRPC_PORT:-443}"
MACOS_CA_BUNDLE="${MACOS_CA_BUNDLE:-/etc/ssl/cert.pem}"
WORKSPACE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BAZEL_TARGET="//src/client:tbox_client"
LOCAL_BINARY="${WORKSPACE_ROOT}/bazel-bin/src/client/${BINARY_NAME}"
STAGE_DIR="$(mktemp -d /tmp/tbox-macmini.XXXXXX)"

cleanup() {
    find "${STAGE_DIR}" -mindepth 1 -delete 2>/dev/null || true
    rmdir "${STAGE_DIR}" 2>/dev/null || true
}
trap cleanup EXIT

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "deploy_macmini.sh must run on macOS." >&2
    exit 1
fi

for command in bazel python3 scp sudo; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "Missing required command: ${command}" >&2
        exit 1
    fi
done

echo "[1/7] Building ${BAZEL_TARGET}"
cd "${WORKSPACE_ROOT}"
bazel build "${BAZEL_TARGET}"

if [[ ! -x "${LOCAL_BINARY}" ]]; then
    echo "Missing built binary: ${LOCAL_BINARY}" >&2
    exit 1
fi

echo "[2/7] Staging binary"
STAGED_BINARY="${STAGE_DIR}/${BINARY_NAME}"
cp "${LOCAL_BINARY}" "${STAGED_BINARY}"
chmod u+w "${STAGED_BINARY}"
/usr/bin/strip -x "${STAGED_BINARY}"

echo "[3/7] Fetching configuration from ${OPENWRT_REMOTE}"
STAGED_CONFIG="${STAGE_DIR}/client_config.json"
scp -q "${OPENWRT_REMOTE}:${OPENWRT_CONFIG}" "${STAGED_CONFIG}"
python3 - "${STAGED_CONFIG}" "${TBOX_CLIENT_ID}" "${TBOX_USER}" \
    "${TBOX_SERVER_ADDR}" "${TBOX_GRPC_PORT}" <<'PY'
import hashlib
import json
import os
import string
import sys

path, client_id, user, server_addr, grpc_port = sys.argv[1:]
with open(path, encoding="utf-8") as config_file:
    config = json.load(config_file)

config["client_id"] = client_id
config["user"] = user
config["server_addr"] = server_addr
config["grpc_server_port"] = int(grpc_port)
config["local_cert_path"] = "./conf/ca-bundle.pem"
config["ssh_private_key_path"] = "/var/root/.ssh/id_ed25519"
config["ssh_public_key_path"] = "/var/root/.ssh/id_ed25519.pub"
config["vlmcsd_listen_addresses"] = ["127.0.0.1", "::1"]
password = config.get("password", "")
if len(password) != 64 or any(char not in string.hexdigits for char in password):
    config["password"] = hashlib.sha256(password.encode("utf-8")).hexdigest()

temporary_path = path + ".new"
with open(temporary_path, "w", encoding="utf-8") as config_file:
    json.dump(config, config_file, indent=2)
    config_file.write("\n")
os.replace(temporary_path, path)
PY

STAGED_CERT="${STAGE_DIR}/ca-bundle.pem"
if [[ ! -s "${MACOS_CA_BUNDLE}" ]]; then
    echo "Missing macOS CA bundle: ${MACOS_CA_BUNDLE}" >&2
    exit 1
fi
cp "${MACOS_CA_BUNDLE}" "${STAGED_CERT}"

STAGED_PRIVATE_KEY="${STAGE_DIR}/id_ed25519"
STAGED_PUBLIC_KEY="${STAGE_DIR}/id_ed25519.pub"
scp -q "${OPENWRT_REMOTE}:/root/.ssh/id_ed25519" "${STAGED_PRIVATE_KEY}"
scp -q "${OPENWRT_REMOTE}:/root/.ssh/id_ed25519.pub" "${STAGED_PUBLIC_KEY}"

echo "[4/7] Preparing launchd service"
STAGED_PLIST="${STAGE_DIR}/${SERVICE_LABEL}.plist"
cat >"${STAGED_PLIST}" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>${SERVICE_LABEL}</string>
    <key>ProgramArguments</key>
    <array>
        <string>${BIN_DIR}/${BINARY_NAME}</string>
    </array>
    <key>WorkingDirectory</key>
    <string>${INSTALL_DIR}</string>
    <key>EnvironmentVariables</key>
    <dict>
        <key>TBOX_LOG_DIR</key>
        <string>${LOG_DIR}</string>
        <key>OPENSSL_armcap</key>
        <string>0</string>
    </dict>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>ThrottleInterval</key>
    <integer>5</integer>
    <key>StandardOutPath</key>
    <string>${LOG_DIR}/launchd.stdout.log</string>
    <key>StandardErrorPath</key>
    <string>${LOG_DIR}/launchd.stderr.log</string>
</dict>
</plist>
EOF
/usr/bin/plutil -lint "${STAGED_PLIST}"

echo "[5/7] Installing into ${INSTALL_DIR}"
sudo mkdir -p "${BIN_DIR}" "${CONF_DIR}" "${DATA_DIR}" "${LOG_DIR}"
sudo launchctl bootout "system/${SERVICE_LABEL}" 2>/dev/null || true
sudo /usr/bin/install -m 755 "${STAGED_BINARY}" "${BIN_DIR}/${BINARY_NAME}.new"
sudo mv -f "${BIN_DIR}/${BINARY_NAME}.new" "${BIN_DIR}/${BINARY_NAME}"
sudo /usr/bin/install -m 600 "${STAGED_CONFIG}" "${CONF_DIR}/client_config.json"
sudo /usr/bin/install -m 644 "${STAGED_CERT}" "${CONF_DIR}/ca-bundle.pem"
sudo rm -f "${CONF_DIR}/xiedeacc.com.ca.cer"
sudo mkdir -p /var/root/.ssh
sudo chmod 700 /var/root/.ssh
sudo /usr/bin/install -o root -g wheel -m 600 \
    "${STAGED_PRIVATE_KEY}" /var/root/.ssh/id_ed25519
sudo /usr/bin/install -o root -g wheel -m 644 \
    "${STAGED_PUBLIC_KEY}" /var/root/.ssh/id_ed25519.pub
sudo /usr/bin/install -o root -g wheel -m 644 "${STAGED_PLIST}" "${PLIST_PATH}"
sudo chown -R root:wheel "${INSTALL_DIR}"
sudo sh -c ": > '${LOG_DIR}/launchd.stdout.log'"
sudo sh -c ": > '${LOG_DIR}/launchd.stderr.log'"

echo "[6/7] Enabling and starting ${SERVICE_LABEL}"
sudo launchctl bootstrap system "${PLIST_PATH}"
sudo launchctl enable "system/${SERVICE_LABEL}"
sudo launchctl kickstart -k "system/${SERVICE_LABEL}"
sleep 3

echo "[7/7] Verifying service"
if ! sudo launchctl print "system/${SERVICE_LABEL}" | grep -q 'state = running'; then
    echo "${SERVICE_LABEL} failed to remain running." >&2
    sudo tail -n 50 "${LOG_DIR}/launchd.stderr.log" 2>/dev/null || true
    exit 1
fi

LOGIN_SUCCEEDED=false
for _ in {1..45}; do
    if sudo grep -q "Successfully logged in" \
        "${LOG_DIR}/launchd.stderr.log" 2>/dev/null; then
        LOGIN_SUCCEEDED=true
        break
    fi
    sleep 1
done

if [[ "${LOGIN_SUCCEEDED}" != true ]]; then
    echo "${SERVICE_LABEL} started but did not log in within 45 seconds." >&2
    sudo tail -n 80 "${LOG_DIR}/launchd.stderr.log" |
        grep -E "Successfully|Failed|ERROR|WARNING|gRPC error" >&2 || true
    exit 1
fi

sudo launchctl print "system/${SERVICE_LABEL}" | sed -n '1,35p'
echo "Deployed ${BINARY_NAME} to ${BIN_DIR}/${BINARY_NAME}"
echo "Configuration: ${CONF_DIR}/client_config.json (${TBOX_CLIENT_ID})"
echo "gRPC endpoint: ${TBOX_SERVER_ADDR}:${TBOX_GRPC_PORT}"
echo "Logs: ${LOG_DIR}"
