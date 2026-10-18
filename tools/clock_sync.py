"""Read Codex account limits locally and send only clock/quota telemetry to DeskHub."""
from __future__ import annotations
import argparse
import json
import logging
import math
import os
from pathlib import Path
import queue
import shutil
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[1]


def normalize_limits(result: dict) -> dict:
    """Select the Codex bucket and identify windows by duration, not position."""
    buckets = result.get("rateLimitsByLimitId") or {}
    bucket = buckets.get("codex")
    if bucket is None:
        fallback = result.get("rateLimits") or {}
        if fallback.get("limitId") not in (None, "codex"):
            raise ValueError("Codex bucket missing")
        bucket = fallback
    windows = {}
    for candidate in (bucket.get("primary"), bucket.get("secondary")):
        if not isinstance(candidate, dict):
            continue
        mins = candidate.get("windowDurationMins")
        role = "five" if mins == 300 else "week" if mins == 10080 else None
        used = candidate.get("usedPercent")
        if role is None or isinstance(used, bool) or not isinstance(used, (int, float)) or not math.isfinite(used):
            continue
        reset = candidate.get("resetsAt")
        if isinstance(reset, bool) or not isinstance(reset, int) or not 1704067200 <= reset < 4102444800:
            reset = None
        windows[role] = {"remaining": round(max(0.0, min(100.0, 100.0-used)), 2), "reset": reset}
    if not windows:
        raise ValueError("No usable Codex windows")
    return {"five": windows.get("five"), "week": windows.get("week")}


class AccountClient:
    def __init__(self):
        self.process = None
        self.messages = queue.Queue(maxsize=128)
        self.sequence = 0

    def close(self):
        p, self.process = self.process, None
        if p is not None:
            p.terminate()
            try: p.wait(timeout=3)
            except subprocess.TimeoutExpired: p.kill(); p.wait(timeout=3)
            for stream in (p.stdin, p.stdout):
                if stream: stream.close()

    def start(self):
        exe = shutil.which("codex")
        if not exe:
            # Explorer-launched pythonw may not inherit Codex's terminal PATH.
            installed = list((Path(os.environ.get("LOCALAPPDATA", Path.home()/"AppData/Local")) /
                              "OpenAI/Codex/bin").glob("*/codex.exe"))
            if installed: exe = str(max(installed, key=lambda p: p.stat().st_mtime))
        if not exe:
            raise RuntimeError("Codex executable not found")
        self.messages = queue.Queue(maxsize=128)
        self.process = subprocess.Popen(
            [exe, "app-server", "--stdio"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, encoding="utf-8", bufsize=1,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        stream, messages = self.process.stdout, self.messages
        def read():
            for line in stream:
                try:
                    item = json.loads(line)
                    if messages.full(): messages.get_nowait()
                    messages.put_nowait(item)
                except (ValueError, queue.Full, queue.Empty):
                    pass
        threading.Thread(target=read, daemon=True).start()
        self.request("initialize", {"clientInfo": {"name": "deskhub_clock_sync", "version": "1.2.0"},
                                    "capabilities": {"experimentalApi": True}})
        self.process.stdin.write('{"method":"initialized","params":{}}\n')
        self.process.stdin.flush()

    def request(self, method: str, params: dict, timeout: float = 25) -> dict:
        self.sequence += 1
        request_id = self.sequence
        self.process.stdin.write(json.dumps({"id": request_id, "method": method, "params": params})+"\n")
        self.process.stdin.flush()
        deadline = time.monotonic()+timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError("Codex account server exited")
            try: item = self.messages.get(timeout=min(1, max(.01, deadline-time.monotonic())))
            except queue.Empty: continue
            if item.get("id") != request_id: continue
            if "error" in item: raise RuntimeError("Codex account request failed")
            return item.get("result", {})
        raise TimeoutError("Codex account request timed out")

    def limits(self) -> dict:
        try:
            if self.process is None or self.process.poll() is not None: self.start()
            return normalize_limits(self.request("account/rateLimits/read", {}))
        except Exception:
            self.close()
            raise

