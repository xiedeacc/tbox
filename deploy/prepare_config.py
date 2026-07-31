#!/usr/bin/env python3

import argparse
import ipaddress
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

CLIENT_DNS_CREDENTIAL_KEYS = AWS_DNS_KEYS + (
    "dns_provider",
    "cloudflare_api_token",
    "cloudflare_zone_id",
)


def local_vlmcsd_addresses(values: list[str]) -> list[str]:
    addresses = {"127.0.0.1", "::1"}
    ipv4_lan_networks = (
        ipaddress.ip_network("10.0.0.0/8"),
        ipaddress.ip_network("172.16.0.0/12"),
        ipaddress.ip_network("192.168.0.0/16"),
    )
    ipv6_ula_network = ipaddress.ip_network("fc00::/7")
    for value in values:
        try:
            address = ipaddress.ip_address(value)
        except ValueError:
            continue
        is_lan = (
            address.version == 4
            and any(address in network for network in ipv4_lan_networks)
        ) or (address.version == 6 and address in ipv6_ula_network)
        if is_lan:
            addresses.add(address.compressed)
    return sorted(addresses)


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
        config["write_logs"] = False
        # Clients send monitor_domains and addresses to the server; DNS
        # provider credentials never leave the server.
        for key in CLIENT_DNS_CREDENTIAL_KEYS:
            config.pop(key, None)
        config["vlmcsd_listen_addresses"] = local_vlmcsd_addresses(
            config.get("vlmcsd_listen_addresses", [])
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
        config["write_logs"] = True
        config.pop("monitor_domains", None)
        config.pop("ddns_record_types", None)
        config["certificate_sync_client_ids"] = ["home-nas-001"]
        config["ssh_private_key_path"] = "/home/ubuntu/.ssh/id_ed25519"
        config["ssh_public_key_path"] = "/home/ubuntu/.ssh/id_ed25519.pub"

    args.destination.write_text(
        json.dumps(config, indent=2) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
