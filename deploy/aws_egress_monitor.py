#!/usr/bin/env python3
"""Low-overhead per-process TCP egress sampler for small EC2 instances."""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
import re
import shutil
import subprocess
import time
from collections import defaultdict
from pathlib import Path


BYTES_SENT_RE = re.compile(r"\bbytes_sent:(\d+)\b")
CGROUP_RE = re.compile(r"\bcgroup:(\S+)")
INO_RE = re.compile(r"\bino:(\d+)\b")
UID_RE = re.compile(r"\buid:(\d+)\b")
SOCKET_RE = re.compile(r"socket:\[(\d+)\]")


def read_text(path: Path, limit: int = 4096) -> str:
    try:
        data = path.read_bytes()[:limit]
    except OSError:
        return ""
    return data.replace(b"\x00", b" ").decode("utf-8", "replace").strip()


def scan_process_sockets() -> dict[str, dict[str, object]]:
    sockets: dict[str, dict[str, object]] = {}
    proc = Path("/proc")
    for entry in proc.iterdir():
        if not entry.name.isdigit():
            continue
        pid = entry.name
        fd_dir = entry / "fd"
        if not fd_dir.is_dir():
            continue
        comm = read_text(entry / "comm", 256)
        cmdline = read_text(entry / "cmdline", 2048) or comm
        try:
            exe = os.readlink(entry / "exe")
        except OSError:
            exe = ""
        cgroup = read_text(entry / "cgroup", 2048)
        service = "-"
        for line in cgroup.splitlines():
            if "/system.slice/" in line:
                service = line.rsplit("/system.slice/", 1)[-1].split("/", 1)[0]
                break
        try:
            links = list(fd_dir.iterdir())
        except OSError:
            continue
        for fd in links:
            try:
                target = os.readlink(fd)
            except OSError:
                continue
            match = SOCKET_RE.fullmatch(target)
            if not match:
                continue
            sockets[match.group(1)] = {
                "exe": exe,
                "comm": comm,
                "cmdline": cmdline[:240],
                "service": service,
            }
    return sockets


def host_from_endpoint(endpoint: str) -> str:
    if endpoint.startswith("["):
        end = endpoint.find("]")
        return endpoint[1:end] if end != -1 else endpoint
    if endpoint.count(":") > 1:
        return endpoint.rsplit(":", 1)[0]
    return endpoint.rsplit(":", 1)[0]


def is_internet_peer(local: str, peer: str, include_private: bool) -> bool:
    try:
        local_ip = ipaddress.ip_address(host_from_endpoint(local).strip("[]"))
        peer_ip = ipaddress.ip_address(host_from_endpoint(peer).strip("[]"))
    except ValueError:
        return False
    if local_ip.is_loopback or peer_ip.is_loopback:
        return False
    if include_private:
        return True
    return not (
        peer_ip.is_private
        or peer_ip.is_loopback
        or peer_ip.is_link_local
        or peer_ip.is_multicast
        or peer_ip.is_unspecified
    )


def sample_sockets(include_private: bool) -> list[dict[str, object]]:
    out = subprocess.check_output(
        ["ss", "-tinpeH"], text=True, stderr=subprocess.DEVNULL, timeout=8
    )
    rows: list[dict[str, object]] = []
    lines = out.splitlines()
    i = 0
    while i < len(lines):
        head = lines[i].strip()
        detail = lines[i + 1].strip() if i + 1 < len(lines) else ""
        i += 2
        parts = head.split()
        if len(parts) < 5:
            continue
        local, peer = parts[3], parts[4]
        if not is_internet_peer(local, peer, include_private):
            continue
        bytes_match = BYTES_SENT_RE.search(detail)
        if not bytes_match:
            continue
        ino = (INO_RE.search(head) or INO_RE.search(detail))
        uid = (UID_RE.search(head) or UID_RE.search(detail))
        cgroup = (CGROUP_RE.search(head) or CGROUP_RE.search(detail))
        rows.append(
            {
                "inode": ino.group(1) if ino else f"{local}->{peer}",
                "bytes_sent": int(bytes_match.group(1)),
                "local": local,
                "peer": peer,
                "uid": uid.group(1) if uid else "-",
                "cgroup": cgroup.group(1) if cgroup else "-",
            }
        )
    return rows


def identity(row: dict[str, object], proc_by_inode: dict[str, dict[str, object]]) -> str:
    proc = proc_by_inode.get(str(row["inode"]))
    if proc:
        process_key = proc.get("exe") or proc.get("cmdline") or proc.get("comm")
        return f'{process_key}|{proc["comm"]}|{proc["service"]}'
    return f'unknown|uid={row["uid"]}|{row["cgroup"]}'


def add_bytes(bucket: dict[str, dict[str, object]], key: str, row: dict[str, object],
              proc_by_inode: dict[str, dict[str, object]], delta: int) -> None:
    proc = proc_by_inode.get(str(row["inode"]), {})
    item = bucket.setdefault(
        key,
        {
            "bytes": 0,
            "exe": proc.get("exe", ""),
            "comm": proc.get("comm", "unknown"),
            "cmdline": proc.get("cmdline", ""),
            "service": proc.get("service", row.get("cgroup", "-")),
            "uid": row.get("uid", "-"),
            "sample_peer": row.get("peer", "-"),
        },
    )
    item["bytes"] = int(item["bytes"]) + delta
    item["sample_peer"] = row.get("peer", "-")


def top_items(bucket: dict[str, dict[str, object]], n: int) -> list[dict[str, object]]:
    return sorted(bucket.values(), key=lambda item: int(item["bytes"]), reverse=True)[:n]


def prune(bucket: dict[str, dict[str, object]], limit: int) -> dict[str, dict[str, object]]:
    if len(bucket) <= limit:
        return bucket
    keys = sorted(bucket, key=lambda key: int(bucket[key]["bytes"]), reverse=True)[:limit]
    return {key: bucket[key] for key in keys}


def write_json(path: Path, payload: dict[str, object]) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(payload, separators=(",", ":"), ensure_ascii=True) + "\n")
    tmp.replace(path)


def append_hourly(path: Path, payload: dict[str, object], max_bytes: int) -> None:
    if path.exists() and path.stat().st_size > max_bytes:
        backup = path.with_suffix(path.suffix + ".1")
        try:
            backup.unlink()
        except FileNotFoundError:
            pass
        path.replace(backup)
    with path.open("a", encoding="utf-8") as fh:
        fh.write(json.dumps(payload, separators=(",", ":"), ensure_ascii=True) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--interval", type=int, default=10)
    parser.add_argument("--duration", type=int, default=7 * 24 * 3600)
    parser.add_argument("--out-dir", default="/var/log/tbox-egress-monitor")
    parser.add_argument("--top-n", type=int, default=50)
    parser.add_argument("--include-private", action="store_true")
    parser.add_argument("--min-free-mb", type=int, default=64)
    parser.add_argument("--max-hourly-bytes", type=int, default=5 * 1024 * 1024)
    parser.add_argument("--max-identities", type=int, default=2000)
    args = parser.parse_args()
    args.max_identities = max(args.max_identities, args.top_n)

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    top_path = out_dir / "top.json"
    hourly_path = out_dir / "hourly.jsonl"
    meta_path = out_dir / "meta.json"

    start = int(time.time())
    end = start + args.duration
    write_json(
        meta_path,
        {
            "started_at": start,
            "duration_seconds": args.duration,
            "interval_seconds": args.interval,
            "method": "ss -tinpeH bytes_sent deltas, attributed by socket inode",
            "note": "TCP only; short-lived connections between samples can be missed.",
        },
    )

    last_sent: dict[str, int] = {}
    total: dict[str, dict[str, object]] = {}
    hourly: dict[str, dict[str, object]] = {}
    hour_start = start - (start % 3600)

    while int(time.time()) < end:
        disk = shutil.disk_usage(out_dir)
        if disk.free < args.min_free_mb * 1024 * 1024:
            write_json(top_path, {"error": "low disk space", "free_bytes": disk.free})
            return 2

        now = int(time.time())
        proc_by_inode = scan_process_sockets()
        rows = sample_sockets(args.include_private)
        active_inodes = set()

        for row in rows:
            inode = str(row["inode"])
            active_inodes.add(inode)
            sent = int(row["bytes_sent"])
            previous = last_sent.get(inode)
            last_sent[inode] = sent
            if previous is None or sent < previous:
                continue
            delta = sent - previous
            if delta <= 0:
                continue
            key = identity(row, proc_by_inode)
            add_bytes(total, key, row, proc_by_inode, delta)
            add_bytes(hourly, key, row, proc_by_inode, delta)

        last_sent = {inode: last_sent[inode] for inode in active_inodes if inode in last_sent}
        total = prune(total, args.max_identities)
        hourly = prune(hourly, args.max_identities)

        if now >= hour_start + 3600:
            append_hourly(
                hourly_path,
                {
                    "hour_start": hour_start,
                    "hour_end": now,
                    "top": top_items(hourly, args.top_n),
                },
                args.max_hourly_bytes,
            )
            hourly = {}
            hour_start = now - (now % 3600)

        write_json(
            top_path,
            {
                "updated_at": now,
                "started_at": start,
                "ends_at": end,
                "active_internet_tcp_sockets": len(active_inodes),
                "total_top": top_items(total, args.top_n),
                "current_hour_top": top_items(hourly, args.top_n),
            },
        )
        time.sleep(max(1, args.interval))

    append_hourly(
        hourly_path,
        {"hour_start": hour_start, "hour_end": int(time.time()), "top": top_items(hourly, args.top_n)},
        args.max_hourly_bytes,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
