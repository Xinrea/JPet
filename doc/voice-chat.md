# 语音对话

在设置面板的「语音对话」中填写北京地域的百炼 API Key 和对应业务空间 ID，然后保存。
API Key 存在 macOS 钥匙串或 Windows 凭据管理器中；配置文件只保存业务空间 ID
和是否已设置 Key，不上传到 JPet 云端。

- Mac：按住 Option 说话，松开发送。
- Windows：按住 Ctrl 说话，松开发送。
- 宠物窗口不需要获得焦点。录音、等待和回复状态显示在宠物附近。
- 回复时再次按住快捷键会打断播放并开始下一轮录音。
- 首次在 Mac 使用时需要允许麦克风权限；授权后重新按住 Option 说话。
- 同一连接保留对话上下文；空闲两分钟后断开，下次说话开始新会话。
- 单次录音最长一分钟，松开快捷键后可继续下一轮。回复音量跟随音频设置。

当前使用 `qwen3.8-omni-flash-realtime` 的 WebSocket Manual 模式，仅发送语音。
输入为 16 kHz、单声道、16 bit PCM；输出为 24 kHz PCM。
按键释放后发送 `input_audio_buffer.commit` 和 `response.create`。
打断时取消尚在生成的回复并停止本地播放，同一连接继续保留对话上下文。
当前千问 WebSocket 文档未提供按播放时长截断消息的事件，已生成但未播放的内容仍可能保留在上下文中。

接口说明：[调用指南](https://help.aliyun.com/zh/model-studio/realtime)、
[客户端事件](https://help.aliyun.com/zh/model-studio/client-events)。

协议回归检查不需要 API Key 或音频设备：

```sh
cmake -S . -B build/macos-arm64 -DJPET_BUILD_VOICE_TESTS=ON
cmake --build build/macos-arm64
ctest --test-dir build/macos-arm64 -R voice_session --output-on-failure
```

macOS 原生设置接口检查：`python3 tests/voice_panel_smoke_test.py`。
该检查使用临时数据目录和独立端口，验证启动、退出、配置保存和参数校验，不写入 API Key。
