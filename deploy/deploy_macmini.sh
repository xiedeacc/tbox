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
if [[ "${OPENWRT_REMOTE}" != *@* ]]; then
    echo "OPENWRT_REMOTE must use the explicit user@HostAlias form (for example, root@openwrt)." >&2
    exit 1
fi
OPENWRT_CONFIG="${OPENWRT_CONFIG:-/usr/local/tbox/conf/client_config.json}"
TBOX_CLIENT_ID="${TBOX_CLIENT_ID:-home-macmini-001}"
TBOX_USER="${TBOX_USER:-}"
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

for command in bazel curl python3 scp sudo; do
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
import ipaddress
import json
import os
import re
import string
import subprocess
import sys

path, client_id, user, server_addr, grpc_port = sys.argv[1:]
with open(path, encoding="utf-8") as config_file:
    config = json.load(config_file)

config["client_id"] = client_id
if user:
    config["user"] = user
config["server_addr"] = server_addr
config["grpc_server_port"] = int(grpc_port)
config["write_logs"] = False
config["local_cert_path"] = "./conf/ca-bundle.pem"
config["ssh_private_key_path"] = "/var/root/.ssh/id_ed25519"
config["ssh_public_key_path"] = "/var/root/.ssh/id_ed25519.pub"
vlmcsd_addresses = {"127.0.0.1", "::1"}
ipv4_lan_networks = (
    ipaddress.ip_network("10.0.0.0/8"),
    ipaddress.ip_network("172.16.0.0/12"),
    ipaddress.ip_network("192.168.0.0/16"),
)
ipv6_ula_network = ipaddress.ip_network("fc00::/7")
interfaces = subprocess.run(
    ["/sbin/ifconfig", "-a"],
    check=True,
    capture_output=True,
    text=True,
).stdout
for value in re.findall(r"\binet6?\s+([0-9a-fA-F:.%]+)", interfaces):
    value = value.split("%", 1)[0]
    try:
        address = ipaddress.ip_address(value)
    except ValueError:
        continue
    is_lan = (
        address.version == 4
        and any(address in network for network in ipv4_lan_networks)
    ) or (address.version == 6 and address in ipv6_ula_network)
    if is_lan:
        vlmcsd_addresses.add(address.compressed)
config["vlmcsd_listen_addresses"] = sorted(vlmcsd_addresses)
for key in (
    "route53_hosted_zone_id",
    "aws_access_key_id",
    "aws_secret_access_key",
    "aws_region",
    "dns_provider",
    "cloudflare_api_token",
    "cloudflare_zone_id",
):
    config.pop(key, None)
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
    <string>/dev/null</string>
    <key>StandardErrorPath</key>
    <string>/dev/null</string>
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
sudo find "${LOG_DIR}" -maxdepth 1 -type f \( -name 'tbox_client*.log*' -o -name 'launchd.*.log' \) -delete

echo "[6/7] Enabling and starting ${SERVICE_LABEL}"
sudo launchctl bootstrap system "${PLIST_PATH}"
sudo launchctl enable "system/${SERVICE_LABEL}"
sudo launchctl kickstart -k "system/${SERVICE_LABEL}"
sleep 3

echo "[7/7] Verifying service"
if ! sudo launchctl print "system/${SERVICE_LABEL}" | grep -q 'state = running'; then
    echo "${SERVICE_LABEL} failed to remain running." >&2
    exit 1
fi

CLIENT_REPORTED=false
for _ in {1..45}; do
    if curl -fsS --max-time 5 "${TBOX_SERVER_ADDR%/}/server" |
        python3 -c 'import json,sys; data=json.load(sys.stdin); raise SystemExit(0 if sys.argv[1] in data.get("registered_clients", {}) else 1)' "${TBOX_CLIENT_ID}"; then
        CLIENT_REPORTED=true
        break
    fi
    sleep 1
done

if [[ "${CLIENT_REPORTED}" != true ]]; then
    echo "${SERVICE_LABEL} started but did not report to the server within 45 seconds." >&2
    exit 1
fi

sudo launchctl print "system/${SERVICE_LABEL}" | sed -n '1,35p'
echo "Deployed ${BINARY_NAME} to ${BIN_DIR}/${BINARY_NAME}"
echo "Configuration: ${CONF_DIR}/client_config.json (${TBOX_CLIENT_ID})"
echo "gRPC endpoint: ${TBOX_SERVER_ADDR}:${TBOX_GRPC_PORT}"
echo "Logging: disabled by client configuration"
