#!/usr/bin/env python3

import argparse
import json
from pathlib import Path


CERTIFICATE_FILES = [
    "xiedeacc.com.ca.cer",
    "xiedeacc.com.cer",
    "xiedeacc.com.fullchain.cer",
    "xiedeacc.com.key",
    "xiedeacc.com.ocsp.der",
]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=("client", "server"))
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--host", choices=("nas", "openwrt", "other"))
    parser.add_argument("--credentials-from", type=Path)
    args = parser.parse_args()

    config = json.loads(args.source.read_text(encoding="utf-8"))
    config["certificate_files"] = CERTIFICATE_FILES
    config["certificate_path"] = "/etc/nginx/ssl"

    if args.kind == "client":
        if args.host is None:
            parser.error("--host is required for client configurations")
        config["server_addr"] = "https://ip.xiedeacc.com"
        config["grpc_server_port"] = 443
        config["local_cert_path"] = "./conf/ca-bundle.pem"
        config["update_certs"] = args.host == "nas"
        if args.credentials_from:
            credentials = json.loads(
                args.credentials_from.read_text(encoding="utf-8")
            )
            config["user"] = credentials["user"]
            config["password"] = credentials["password"]
        ssh_home = "/root" if args.host in ("nas", "openwrt") else "~"
        config["ssh_private_key_path"] = f"{ssh_home}/.ssh/id_ed25519"
        config["ssh_public_key_path"] = f"{ssh_home}/.ssh/id_ed25519.pub"
    else:
        config["certificate_sync_client_ids"] = ["home-nas-001"]
        config["ssh_private_key_path"] = "/home/ubuntu/.ssh/id_ed25519"
        config["ssh_public_key_path"] = "/home/ubuntu/.ssh/id_ed25519.pub"

    args.destination.write_text(
        json.dumps(config, indent=2) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
