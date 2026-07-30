#!/bin/bash

set -euo pipefail

REMOTE="root@aws"
WORKSPACE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

scp "${WORKSPACE_ROOT}/deploy/aws_egress_monitor.py" \
    "${REMOTE}:/usr/local/sbin/tbox-egress-monitor.py"
scp "${WORKSPACE_ROOT}/deploy/tbox_egress_monitor.service" \
    "${REMOTE}:/etc/systemd/system/tbox-egress-monitor.service"

ssh "${REMOTE}" "\
chmod 755 /usr/local/sbin/tbox-egress-monitor.py && \
mkdir -p /var/log/tbox-egress-monitor && \
systemctl daemon-reload && \
systemctl enable tbox-egress-monitor.service && \
systemctl restart tbox-egress-monitor.service && \
systemctl is-active --quiet tbox-egress-monitor.service"

echo "Started tbox-egress-monitor.service on ${REMOTE}"
