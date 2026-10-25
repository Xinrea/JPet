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
            assert code == 200 and not {"api_key", "has_api_key", "workspace_id"} & config.keys()
            # No chat service is chosen until the user picks one; each service keeps its own key.
            assert config["input_mode"] == "toggle" and config["chat_provider"] == "" and config["chat_model"] == "" and config["chat_base_url"] == ""
            assert [provider["id"] for provider in config["chat_providers"]] == ["deepseek", "openrouter", "thirdparty"]
            assert config["chat_keys"] == {"deepseek": False, "openrouter": False, "thirdparty": False} and config["chat_vision"] is False
            assert config["chat_context_window"] == 32768
            for invalid in [None, False, "32768", 32768.0, 4095, -1, 10_000_001, 2**64 - 1]:
                assert request(port, "/api/config/voice", {"chat_context_window": invalid})[0] == 400, invalid
            # Speech is synthesised on this machine in Joi's voice: no synthesis model or voice to choose.
            assert not {"provider", "model", "voice", "engine", "cascade", "stt", "tts_model", "tts_voice"} & config.keys()
            assert "catalog" not in config and "has_chat_api_key" not in config
            # Mode can be saved before credentials are configured.
            assert request(port, "/api/config/voice", {"input_mode": "hold"})[1]["input_mode"] == "hold"
            for invalid_mode in ["", "invalid", None, 123, False]:
                assert request(port, "/api/config/voice", {"input_mode": invalid_mode})[0] == 400
            assert request(port, "/api/config/voice")[1]["input_mode"] == "hold"
            # The shortcut is recorded as KeyboardEvent.code keys; the default is the platform's previous fixed key.
            assert config["shortcut"] in ("Alt", "Control") and config["shortcut_default"] is True and config["shortcut_name"] in ("Option", "Ctrl")
            code, saved = request(port, "/api/config/voice", {"shortcut": "Space+ControlRight"})
            assert code == 200 and saved["shortcut"] == "ControlRight+Space" and saved["shortcut_default"] is False, saved
            assert saved["shortcut_name"].endswith(" + 空格") and saved["input_mode"] == "hold"
            for invalid_key, error in [("KeyA", "打字"), ("ShiftLeft+KeyA", "打字"), ("Fn", "快捷键"), ("Control+", "快捷键"),
                                       ("ControlLeft+AltLeft+MetaLeft+ShiftLeft+KeyK", "4 个键"), (None, "快捷键"), (1, "快捷键")]:
                code, result = request(port, "/api/config/voice", {"shortcut": invalid_key})
                assert code == 400 and error in result["error"], (invalid_key, result)
            assert request(port, "/api/config/voice", {"input_mode": "hold"})[1]["shortcut"] == "ControlRight+Space"
            assert request(port, "/api/voice")[1]["shortcut"] == saved["shortcut_name"]
            # Recording a new shortcut pauses the saved one.
            assert request(port, "/api/voice/shortcut-recording", {"active": True})[0] == 200
            assert request(port, "/api/voice/shortcut-recording", {"active": False})[0] == 200
            for invalid in [{}, {"active": "yes"}, {"active": 1}]:
                assert request(port, "/api/voice/shortcut-recording", invalid)[0] == 400, invalid
            assert request(port, "/api/voice/shortcut-recording", {"active": True}, "text/plain")[0] == 403
            # Microphone and speaker default to the system devices; a saved id need not be connected.
            assert config["input_device"] == "" and config["output_device"] == ""
            assert all(isinstance(config["audio_devices"][kind], list) for kind in ("inputs", "outputs"))
            assert all(device["id"] for device in config["audio_devices"]["inputs"] + config["audio_devices"]["outputs"])
            code, saved = request(port, "/api/config/voice", {"input_device": "unplugged-mic", "output_device": "unplugged-speaker"})
            assert code == 200 and saved["input_device"] == "unplugged-mic" and saved["output_device"] == "unplugged-speaker"
            assert request(port, "/api/config/voice", {"input_mode": "toggle"})[1]["output_device"] == "unplugged-speaker"
            for invalid in [{"input_device": "mic\nother"}, {"output_device": 1}, {"output_device": "x" * 257}]:
                assert request(port, "/api/config/voice", invalid)[0] == 400, invalid
            assert request(port, "/api/config/voice", {"input_device": "", "output_device": "", "input_mode": "hold"})[1]["input_device"] == ""
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{port}/api/powerlive")
                raise AssertionError("the PowerLive account API is gone")
            except urllib.error.HTTPError as missing:
                assert missing.code == 404
            voice = request(port, "/api/voice")[1]
            assert voice["microphone_on"] is False and voice["input_mode"] == "hold" and voice["model"] == ""
            assert set(voice["available_tools"]) == {"get_game_profile", "get_game_clothes", "get_task_catalog", "get_current_task", "get_task_queue", "get_task_history", "get_game_achievements", "get_game_statistics", "game_action", "jpet_settings", "web_search", "web_fetch", "bilibili", "open_url"}  # view_desktop needs a model that reads images
            # Recognition (SenseVoice) and synthesis (Qwen3-TTS) download as one model set.
            assert voice["speech_model"]["state"] == "missing" and voice["speech_model"]["total"] > 1_500_000_000
            code, history = request(port, "/api/voice/history")
            assert code == 200 and history == {"list": [], "limit": 200, "error": ""}
            # Typed chat needs a message and the AI service settings; nothing is queued without them.
            assert voice["replying"] is False
            for message, error in [({"text": "  "}, "消息不能为空"), ({"text": 1}, "消息格式不正确"), ({"text": "你好"}, "大模型服务")]:
                code, result = request(port, "/api/voice/chat", message)
                assert code == 400 and error in result["error"], result
            assert request(port, "/api/voice/chat/stop", {})[0] == 200
            # These requests never create a credential in the real keychain.
            for invalid in [{"chat_provider": "deepseek", "chat_api_key": "key with spaces"},
                            {"chat_provider": "deepseek", "chat_api_key": "sk-x\r\nInjected: yes"},
                            {"chat_provider": "deepseek", "chat_model": "m", "chat_vision": "yes"},
                            {"chat_api_key": "sk-no-service"}, {"chat_provider": "bailian"}, {"chat_provider": 1},
                            {"chat_provider": "thirdparty"}, {"chat_provider": "thirdparty", "chat_base_url": "http://example.com/v1"},
                            {"chat_provider": "thirdparty", "chat_base_url": "https://user:pw@example.com"},
                            {"chat_provider": "deepseek", "chat_model": "bad model"}]:
                assert request(port, "/api/config/voice", invalid)[0] == 400, invalid
            # Model lists: a known service, a valid endpoint and a key are checked before any request is sent.
            for invalid, error in [({"provider": "bailian"}, "大模型服务"), ({"provider": "deepseek"}, "API Key"),
                                   ({"provider": "deepseek", "api_key": "bad key"}, "API Key"),
                                   ({"provider": "thirdparty", "base_url": "http://example.com/v1", "api_key": "sk-x"}, "接口地址"),
                                   ({"provider": "thirdparty", "base_url": "https://example.com/v1"}, "API Key")]:
                code, result = request(port, "/api/voice/chat-models", invalid)
                assert code == 400 and error in result["error"], (invalid, result)
            assert request(port, "/api/voice/chat-models", {"provider": "openrouter"}, "text/plain")[0] == 403
            assert request(port, "/api/config/voice", {"input_mode": "hold"}, "text/plain")[0] == 403
            # Fields of the removed CosyVoice synthesis are ignored, not errors.
            code, saved = request(port, "/api/config/voice", {"chat_provider": "thirdparty", "chat_model": "llama3",
                                                              "chat_base_url": "http://127.0.0.1:11434/v1",
                                                              "tts_model": "cosyvoice-v3-plus", "tts_voice": "longanyang"})
            assert code == 200 and saved["chat_provider"] == "thirdparty" and saved["chat_model"] == "llama3"
            assert saved["chat_base_url"] == "http://127.0.0.1:11434/v1"
            assert "tts_voice" not in saved and saved["chat_keys"]["thirdparty"] is False and saved["chat_vision"] is False
            voice = request(port, "/api/voice")[1]
            assert voice["model"] == "llama3" and "view_desktop" not in voice["available_tools"]
            # A model that reads images may look at the desktop; another model starts text-only.
            assert request(port, "/api/config/voice", {"chat_vision": True})[1]["chat_vision"] is True
            assert "view_desktop" in request(port, "/api/voice")[1]["available_tools"]
            assert request(port, "/api/config/voice", {"chat_model": "llama3.2"})[1]["chat_vision"] is False
            assert request(port, "/api/config/voice", {"chat_model": "llama3", "chat_vision": True})[1]["chat_vision"] is True
            # Context capacity is retained by unrelated edits, but belongs to the model and endpoint.
            assert request(port, "/api/config/voice", {"chat_context_window": 65536})[1]["chat_context_window"] == 65536
            assert request(port, "/api/config/voice", {"input_mode": "hold"})[1]["chat_context_window"] == 65536
            assert request(port, "/api/config/voice", {"chat_model": "llama3.2"})[1]["chat_context_window"] == 32768
            assert request(port, "/api/config/voice", {"chat_model": "llama3", "chat_context_window": 4096})[1]["chat_context_window"] == 4096
            assert request(port, "/api/config/voice", {"chat_base_url": "http://127.0.0.1:11435/v1"})[1]["chat_context_window"] == 32768
            assert request(port, "/api/config/voice", {"chat_context_window": 10_000_000})[1]["chat_context_window"] == 10_000_000
            # Without a saved key the chat is refused before anything is queued.
            code, result = request(port, "/api/voice/chat", {"text": "你好"})
            assert code == 400 and "API Key" in result["error"], result
            # Another service drops the third-party model and endpoint.
            code, saved = request(port, "/api/config/voice", {"chat_provider": "deepseek"})
            assert code == 200 and saved["chat_provider"] == "deepseek" and saved["chat_model"] == "" and saved["chat_base_url"] == ""
            assert saved["chat_context_window"] == 32768
            code, saved = request(port, "/api/config/voice", {"chat_model": "deepseek-chat", "input_mode": "hold"})
            assert code == 200 and saved["chat_provider"] == "deepseek" and saved["chat_model"] == "deepseek-chat"
            assert saved["chat_context_window"] == 32768
            assert request(port, "/api/config/voice", {"chat_context_window": 65536})[1]["chat_context_window"] == 65536
            assert request(port, "/api/config/voice", {"chat_provider": "openrouter", "chat_model": "deepseek-chat"})[1]["chat_context_window"] == 32768
            assert request(port, "/api/config/voice", {"chat_provider": "deepseek", "chat_model": "deepseek-chat"})[1]["chat_context_window"] == 32768
            code, model = request(port, "/api/voice/speech-model", {"action": "cancel"})
            assert code == 200 and model["state"] == "missing"
            assert request(port, "/api/voice/speech-model", {"action": "format"})[0] == 400
            assert request(port, "/api/voice/speech-model", {"action": "download"}, "text/plain")[0] == 403
            contents = Path(profile, "jpet.toml").read_text()
            assert re.search(r'^\s*input_mode\s*=\s*[\"\']hold[\"\']', contents, re.MULTILINE)
            assert re.search(r'^\s*shortcut\s*=\s*[\"\']ControlRight\+Space[\"\']', contents, re.MULTILINE)
            # An empty shortcut restores the default, saved empty so it follows the platform.
            code, saved = request(port, "/api/config/voice", {"shortcut": ""})
            assert code == 200 and saved["shortcut"] == config["shortcut"] and saved["shortcut_default"] is True
            assert re.search(r'^\s*shortcut\s*=\s*[\"\'][\"\']', Path(profile, "jpet.toml").read_text(), re.MULTILINE)
            assert not re.search(r'^\s*tts_(model|voice)\s*=', contents, re.MULTILINE)
            assert not re.search(r"^\s*(api_key|chat_api_key|provider|engine|has_chat_api_key|has_api_key|workspace_id)\s*=", contents, re.MULTILINE)
            assert re.search(r'^\s*chat_provider\s*=\s*[\"\']deepseek[\"\']', contents, re.MULTILINE)
            assert re.search(r'^\s*chat_context_window\s*=\s*32768\s*$', contents, re.MULTILINE)
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
