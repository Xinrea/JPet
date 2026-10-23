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
            assert config["input_mode"] == "toggle" and config["chat_model"] == "qwen-plus" and config["chat_base_url"] == ""
            assert config["tts_model"] == "cosyvoice-v3-flash" and config["tts_voice"] == "longanhuan"
            assert {model["id"] for model in config["catalog"]["tts_models"]} == {"cosyvoice-v3-flash", "cosyvoice-v3-plus"}
            assert not {"provider", "model", "voice", "engine", "cascade", "stt"} & config.keys()
            # Mode can be saved before credentials are configured.
            assert request(port, "/api/config/voice", {"input_mode": "hold"})[1]["input_mode"] == "hold"
            for invalid_mode in ["", "invalid", None, 123, False]:
                assert request(port, "/api/config/voice", {"input_mode": invalid_mode})[0] == 400
            assert request(port, "/api/config/voice")[1]["input_mode"] == "hold"
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{port}/api/powerlive")
                raise AssertionError("the PowerLive account API is gone")
            except urllib.error.HTTPError as missing:
                assert missing.code == 404
            voice = request(port, "/api/voice")[1]
            assert voice["microphone_on"] is False and voice["input_mode"] == "hold" and voice["model"] == "qwen-plus"
            assert set(voice["available_tools"]) == {"view_desktop", "get_game_profile", "get_game_clothes", "get_task_catalog", "get_current_task", "get_task_queue", "get_task_history", "get_game_achievements", "get_game_statistics", "get_game_rank", "game_action", "jpet_settings", "web_search", "bilibili_search", "open_url"}
            assert voice["speech_model"]["state"] == "missing" and voice["speech_model"]["total"] > 200_000_000
            code, history = request(port, "/api/voice/history")
            assert code == 200 and history == {"list": [], "limit": 200, "error": ""}
            # These requests never create a credential in the real keychain.
            assert request(port, "/api/config/voice", {"workspace_id": "ws-123"})[0] == 200
            assert request(port, "/api/config/voice")[1]["workspace_id"] == "ws-123"
            for invalid in [{"workspace_id": "evil.example"}, {"workspace_id": 123},
                            {"workspace_id": "ws-123", "api_key": "sk-x\r\nInjected: yes"},
                            {"chat_api_key": "key with spaces"}, {"chat_base_url": "http://example.com/v1"},
                            {"chat_base_url": "https://user:pw@example.com"}, {"chat_model": "bad model"},
                            {"tts_model": "cosyvoice-v9"}, {"tts_model": "cosyvoice-v3-plus", "tts_voice": "longhua_v3"}]:
                assert request(port, "/api/config/voice", invalid)[0] == 400, invalid
            assert request(port, "/api/config/voice", {"workspace_id": "ws-123"}, "text/plain")[0] == 403
            code, saved = request(port, "/api/config/voice", {"chat_model": "deepseek-chat", "chat_base_url": "https://api.deepseek.com/v1",
                                                              "tts_model": "cosyvoice-v3-plus", "tts_voice": "longanyang"})
            assert code == 200 and saved["chat_model"] == "deepseek-chat" and saved["chat_base_url"] == "https://api.deepseek.com/v1"
            assert saved["tts_voice"] == "longanyang" and saved["has_chat_api_key"] is False and saved["workspace_id"] == "ws-123"
            code, saved = request(port, "/api/config/voice", {"tts_model": "cosyvoice-v3-flash"})
            assert code == 200 and saved["tts_voice"] == "longanyang", "a voice offered by the new model is kept"
            assert request(port, "/api/voice")[1]["model"] == "deepseek-chat"
            code, model = request(port, "/api/voice/speech-model", {"action": "cancel"})
            assert code == 200 and model["state"] == "missing"
            assert request(port, "/api/voice/speech-model", {"action": "format"})[0] == 400
            assert request(port, "/api/voice/speech-model", {"action": "download"}, "text/plain")[0] == 403
            contents = Path(profile, "jpet.toml").read_text()
            assert re.search(r'^\s*input_mode\s*=\s*[\"\']hold[\"\']', contents, re.MULTILINE)
            assert re.search(r'^\s*tts_voice\s*=\s*[\"\']longanyang[\"\']', contents, re.MULTILINE)
            assert "workspace_id" in contents and not re.search(r"^\s*(api_key|chat_api_key|provider|engine)\s*=", contents, re.MULTILINE)
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
