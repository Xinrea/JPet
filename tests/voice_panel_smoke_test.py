"""Exercise the real panel API with an isolated native macOS smoke-test profile."""

import argparse
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "build/macos-arm64/bin/JPet/JPet.app/Contents/MacOS/JPet"


def request(port, path, payload=None, content_type="application/json"):
    body = None if payload is None else json.dumps(payload).encode()
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}" + path,
        data=body,
        headers={"Content-Type": content_type},
    )
    try:
        with urllib.request.urlopen(req, timeout=1) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        return error.code, json.load(error)


def main(gui=False, app=APP):
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="jpet-voice-smoke-") as profile:
        env = dict(os.environ, JPET_DATA_DIR=profile, JPET_SMOKE_PORT=str(port))
        if gui:
            # The window smoke mode intentionally skips the panel server.
            # Verify its lifecycle separately, then exercise the panel API.
            window = subprocess.run([str(app), "--smoke-test"], env=env,
                                    capture_output=True, timeout=15)
            if window.returncode:
                raise RuntimeError(f"Native window startup failed ({window.returncode}): "
                                   + (window.stdout + window.stderr).decode(errors="replace")[-2000:])
            print("PASS native pet window startup/exit")
        process = subprocess.Popen([str(app), "--panel-smoke-test"], env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 4
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    output, errors = process.communicate()
                    raise RuntimeError(f"Native app startup failed ({process.returncode}): "
                                       + (output + errors).decode(errors="replace")[-2000:])
                try:
                    code, config = request(port, "/api/config/voice")
                    break
                except urllib.error.URLError:
                    time.sleep(0.05)
            else:
                raise RuntimeError("Panel server did not start")
            assert code == 200 and config["has_api_key"] is False and "api_key" not in config
            assert config["provider"] == "custom"
            assert request(port, "/api/config/voice", {"provider": "jpet"})[0] == 200
            assert request(port, "/api/config/voice")[1]["provider"] == "jpet"
            assert request(port, "/api/config/voice", {"provider": "invalid"})[0] == 400
            assert request(port, "/api/config/voice", {"provider": "custom"})[0] == 200
            account = request(port, "/api/powerlive")[1]
            assert account["logged_in"] is False and account["user"] is None
            assert not {"access_token", "refresh_token", "id_token"} & account.keys()
            for route in ["/api/powerlive/login", "/api/powerlive/logout"]:
                assert request(port, route, {}, "text/plain")[0] == 403
                forged = urllib.request.Request(f"http://127.0.0.1:{port}" + route, data=b"{}", headers={"Content-Type": "application/json", "Origin": "https://evil.example"})
                try:
                    urllib.request.urlopen(forged)
                    raise AssertionError("Cross-origin account mutation accepted")
                except urllib.error.HTTPError as rejected:
                    assert rejected.code == 403
            assert request(port, "/api/powerlive/logout", {})[0] == 200
            voice = request(port, "/api/voice")[1]
            assert voice["microphone_on"] is False
            assert voice["model"] == "qwen3.8-omni-flash-realtime"
            assert set(voice["available_tools"]) == {"view_desktop", "get_game_state", "game_action", "jpet_settings", "web_search", "bilibili_search", "open_url"}
            code, history = request(port, "/api/voice/history")
            assert code == 200 and history == {"list": [], "limit": 200, "error": ""}
            # These requests never create a credential in the real keychain.
            assert request(port, "/api/config/voice", {"workspace_id": "ws-123"})[0] == 200
            assert request(port, "/api/config/voice")[1]["workspace_id"] == "ws-123"
            for invalid in [{"workspace_id": "evil.example"}, {"workspace_id": 123},
                            {"workspace_id": "ws-123", "api_key": "sk-x\r\nInjected: yes"}]:
                assert request(port, "/api/config/voice", invalid)[0] == 400
            assert request(port, "/api/config/voice", {"workspace_id": "ws-123"}, "text/plain")[0] == 403
            assert request(port, "/api/config/voice")[1]["workspace_id"] == "ws-123"
            contents = Path(profile, "jpet.toml").read_text()
            assert "workspace_id" in contents and not re.search(r"^\s*api_key\s*=", contents, re.MULTILINE)
            output, errors = process.communicate(timeout=10)
            if process.returncode:
                raise RuntimeError((output + errors).decode(errors="replace")[-2000:])
            print("PASS native startup/exit, voice settings API, persistence, secret redaction and validation")
        finally:
            if process.poll() is None:
                process.terminate()
                process.communicate(timeout=5)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gui", action="store_true", help="Also create the native pet window; requires a display")
    parser.add_argument("--app", type=Path, default=APP, help="Native executable to verify")
    args = parser.parse_args()
    main(args.gui, args.app)
