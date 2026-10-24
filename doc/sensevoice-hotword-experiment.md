# SenseVoice CTC 热词实验

## 结论与范围

**不换 SenseVoice 模型，通过解码时的热词加权可以改变专有名词识别结果；权重过大会误伤同音普通词。**
下表保留接入前的独立实验结果。随后按用户选择将 **权重 12、beam=8、热词「轴伊」** 接入应用，
设置面板没有新增选项；后续需要时再调整权重。当前运行方式见 [语音对话文档](voice-chat.md#本机识别与轮次检测)。

补丁保存在 `build-scripts/ports/sherpa-onnx-bin/sensevoice-hotwords.patch`，固定上游 v1.13.8，
无需维护单独的 fork。升级上游时仍需重新检查补丁、重新构建和测试。

实验使用 `jpet_ctc_beam_search`：CTC prefix beam search 合并同一文本前缀的 blank/nonblank 路径，
复用 sherpa 的 ContextGraph 进行热词加分；未完成词的临时奖励在结束时收回。
SenseVoice 的前四个元数据帧保持独立 greedy 解码，不参与热词加权。
普通 `greedy_search` 行为不变，C API 结构布局不变。接入应用时补充了 capability 版本导出，
应用创建识别器前检查该符号，避免误加载官方库后在不支持的解码路径退出；新增文件读取复用上游的 Windows UTF-8 路径支持。

热词文件每行是一组**模型词表中的 token**，例如 `轴 伊`，不是任意自然语言提示。
当前支持识别器级文件和统一权重，不支持自动分词、逐条权重、动态 per-stream 热词或聊天上下文。

## 本次测量：仅合成语音

- 机器：Apple M2 Max，macOS 27.0.1 arm64；CPU 推理，2 线程。
- 模型：现有 `sensevoice-int8-2025-09-09/model.int8.onnx`；language=`auto`，ITN 开启。
- sherpa-onnx 1.13.8 + 本仓库补丁，ONNX Runtime 1.28.2；beam width=8。
- 热词只有「轴伊」。Tingting、Meijia 各朗读 21 句，共 42 条：14 条含热词正例、14 条近音负例、14 条普通对照。
- 音频只生成一次，保存成 16 kHz mono PCM16 WAV；三种识别器和各个权重使用相同音频。
- 三个识别器均预热；逐条轮换运行顺序。耗时只计 decode，不含模型加载、音频读取、特征接收和结果提取。
- CER：NFKC/casefold、去标点和空白后的字符编辑距离 / 参考字符数；不是专名命中率。

| 解码方式 | 热词权重（每 token） | 正例命中 / 14 | 近音负例误插热词 / 14 | 普通对照误插 / 14 | 全集 CER | 平均 decode |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 原始 greedy | — | 0 | 0 | 0 | 8.43% | 51.9 ms |
| beam，无热词文件 | — | 0 | 0 | 0 | 8.15% | 52.8 ms |
| beam + 热词文件 | 0 | 0 | 0 | 0 | 8.15% | 53.3 ms |
| beam + 热词文件 | 5 | 3 | 0 | 0 | 7.02% | 53.5 ms |
| beam + 热词文件 | 8 | 4 | 0 | 0 | 6.46% | 53.4 ms |
| beam + 热词文件 | 10 | 7 | 0 | 0 | 4.78% | 53.4 ms |
| beam + 热词文件 | 12 | 14 | 1 | 0 | 1.40% | 53.7 ms |
| beam + 热词文件 | 20 | 14 | 10 | 0 | 6.74% | 54.0 ms |

基线取权重 0 那一轮的 greedy/beam；每个权重都重新测了三个配置，基线转写一致。
权重 0 的热词输出在这批音频上与无热词 beam 完全一致；实现中热词候选仍会加入剪枝集合，
所以不承诺所有音频上的逐字等价。单次平均延迟差约几毫秒，不是多轮统计意义上的性能保证。

例子（权重 12）：

| 音色 | 参考内容 | 原始 greedy | 热词解码 |
| --- | --- | --- | --- |
| Tingting | 轴伊你好呀 | 周一你好呀 | 轴伊你好呀 |
| Meijia | 轴伊你好呀 | 周医你好呀 | 轴伊你好呀 |
| Tingting | 轴伊把读书加入任务队列 | 周一把读书加入任务队列 | 轴伊把读书加入任务队列 |
| Tingting | 周一提醒我读书 | 周一提醒我读书 | 周一提醒我读书 |
| Meijia | 这件衣服有一点皱衣领也不平 | 这件衣服有一点皱衣领也不平 | 这件衣服有一点轴伊领也不平 |

权重 20 还会把「下周一有什么任务」改成「下轴伊有什么任务」、把「请逐一检查任务列表」改成「请轴伊检查任务列表」。
这说明不能只追求热词命中，也不能将权重 12 或 20 直接当成默认值。

**这些是系统 TTS 生成的样例，不是真人准确率。** 同一热词、少量句式和两个音色不足以选择阈值；
也未覆盖噪声、多人、不同口音、英文专名、长词表或真实打断场景。稀有名字的 TTS 发音还可能和用户不同。
下一步应补真人录音，包含真正说「周一」「逐一」「注意」的负例和静音，固定一部分样本做独立验证后再决定是否接入应用。

## 复现与检查

完整说明（含真人录音 manifest）见 [实验 README](../tests/README-voice-ctc-hotwords.md)。
从仓库根目录执行：

```sh
python3 -B build-scripts/build_sensevoice_hotwords.py
OUT="$PWD/build/sensevoice-hotwords"
MODEL="$HOME/Library/Application Support/JPet/models/sensevoice-int8-2025-09-09"

# 目录必须未存在；生成一次，整个权重扫描复用。
python3 -B tests/voice_ctc_hotword_probe.py synthetic \
  --output-dir "$OUT/fixtures" --voices Tingting Meijia
mkdir -p "$OUT/final-reports"
for score in 0 5 8 10 12 20; do
  python3 -B tests/voice_ctc_hotword_probe.py run \
    --probe "$OUT/voice_ctc_hotword_probe" --model-dir "$MODEL" \
    --manifest "$OUT/fixtures/manifest.json" --score "$score" \
    --output "$OUT/final-reports/score-$score.json" || break
done
cp "$OUT/build-info.json" "$OUT/final-reports/build-info.json"

"$OUT/voice_ctc_config_test" "$MODEL"
JPET_CTC_PROBE="$OUT/voice_ctc_hotword_probe" \
  python3 -B -m unittest discover -s tests -p 'voice_ctc_*test.py' -v
```

本次原始逐句报告在本机构建目录 `build/sensevoice-hotwords/final-reports/`，WAV 在 `fixtures/`；
这些生成物不提交仓库。JSON 保留参考/转写、逐句延迟、输入 SHA-256；同目录 `build-info.json`
保留上游归档、补丁、运行库和探针哈希。不要在扫描过程中更换 dylib 或设置覆盖库路径的 `DYLD_*` 环境变量。

模型 SHA-256：`12ca1a2ae7ecf3e0019ef2822307ee0b5cadc9196569e379b4c4026f8205276d`。
该轮实验补丁 SHA-256：`ad97612c8b27f3bc2d3157a3f5b432a777a0047e999974837ae23d77f84802b9`。
它不包含后续的运行库 capability 导出和路径兼容改动；重建当前补丁时使用新的构建目录，不能覆盖旧实验的 provenance。

已验证：

- 固定版本源码校验、补丁应用、完整构建和重复构建成功。
- 解码器：900 次穷举路径对照，覆盖重复标签、blank、路径合并、重叠热词和奖励收回；另测元数据、批次有效长度、热词解析和边界。
- ASan + UBSan 下同一解码测试通过（自定义 header 被插桩；预编译 ORT / upstream 静态依赖未全面插桩）。
- C API 的 20 个配置用例通过，包括显式权重 0、非法数值、未知/保留 token 和旧 greedy 配置。
- 15 个 Python/编译后 WAV 解析测试通过，包括归档校验、补丁变化拒绝、符号链接跳过和路径越界拒绝。
- 实际 dyld 加载检查确认使用实验目录的 sherpa/ORT；三个探针 minimum macOS target=11.0，使用相对 runtime 路径。
- 同批 42 条音频的六档权重扫描，输出完整；最终探针哈希与报告吻合。

上述实验阶段未运行完整 JPet GUI/应用测试、Windows 构建、旧 macOS 实机测试或真人录音评估。
应用接入后默认使用热词加权，但本表仍仅代表这些合成样例，不代表真人准确率。
