# 语音轮次检测模型

级联语音引擎在本机运行的 ONNX 模型，均取自 [Pipecat](https://github.com/pipecat-ai/pipecat) 随包发布的文件，未作修改。

| 文件 | 来源 | 许可 |
| --- | --- | --- |
| `silero_vad.onnx` | [Silero VAD](https://github.com/snakers4/silero-vad) v5，16 kHz | MIT |
| `smart-turn-v3.2-cpu.onnx` | [Smart Turn](https://github.com/pipecat-ai/smart-turn) v3.2，int8 量化 | BSD-2-Clause |

SHA-256：

```
1a153a22f4509e292a94e67d6f9b85e8deb25b4988682b7e174c65279d8788e3  silero_vad.onnx
2bb026316b14a660486a75b1733cd3fbab8c2fd0314dc9af7be49f8cca967e4f  smart-turn-v3.2-cpu.onnx
```

## 语音识别热词

`sensevoice-hotwords.txt` 随应用分发，每行是一个短语在 SenseVoice `tokens.txt` 中的 token，用空格分隔。
目前只有 `轴 伊`。应用用打过补丁的 sherpa-onnx CTC beam search 识别，beam=8、每 token 热词权重=12；
权重统一定义在 `src/VoiceSpeech.cpp`。增加词条时必须核对模型词表，不能直接粘贴未分词短语或注释。
当前不会加载聊天上下文，也不会将转写中的同音词强行替换；权重过高仍可能误伤普通近音词。

## 轴伊的声音

`joi-voice.json` 是本机语音合成（Qwen3-TTS 12Hz 0.6B Base）上下文克隆用的音色，已获授权随 JPet 分发：

- 来源：轴伊舰长礼物 2026-09（`202609.mp3`）258.76–266.94 s 的一段朗读，单声道 24 kHz，峰值归一到 0.5。
- 内容：该段录音的说话人向量（1024 维 float32）、16 × 102 的编码（int32），以及该段文字；不包含原始录音。
- 生成：`voice_synthesis_probe reference <合成模型目录> <录音.wav> <文字> <来源说明> joi-voice.json`，
  模型为 `qwen-talker-0.6b-base-Q8_0.gguf` 与 `qwen-tokenizer-12hz-Q8_0.gguf`。更换模型版本时需要重新生成。

