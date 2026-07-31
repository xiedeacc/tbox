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

AWS_DNS_KEYS = (
    "route53_hosted_zone_id",
    "aws_access_key_id",
    "aws_secret_access_key",
    "aws_region",
)

CLIENT_DNS_KEYS = AWS_DNS_KEYS + (
    "dns_provider",
    "cloudflare_api_token",
    "cloudflare_zone_id",
    "monitor_domains",
    "ddns_record_types",
)


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
        # Only NAS performs client-side DDNS for the home network. Other
        # clients report their addresses to the server and must not receive
        # DNS provider credentials.
        keys_to_remove = AWS_DNS_KEYS if args.host == "nas" else CLIENT_DNS_KEYS
        for key in keys_to_remove:
            config.pop(key, None)
        if args.host == "nas" and config.get("monitor_domains"):
            config["dns_provider"] = "cloudflare"
            if not config.get("cloudflare_api_token"):
                parser.error(
                    "NAS DDNS requires cloudflare_api_token in its source config"
                )
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
