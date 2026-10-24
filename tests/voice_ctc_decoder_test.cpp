// Standalone test of the opt-in sherpa-onnx patch, not production JPet.
// Build with patched sherpa source + ORT include paths and link
// libsherpa-onnx-core.a + ONNX Runtime (no model/assets required).
// Alternatively link csrc/{context-graph,symbol-table,text-utils,bbpe,
// base64-decode,file-utils}.cc from the patched sherpa source.
#include "sherpa-onnx/csrc/offline-sense-voice-ctc-beam-search-decoder.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <random>

using sherpa_onnx::OfflineCtcDecoderResult;
using sherpa_onnx::OfflineSenseVoiceCtcBeamSearchDecoder;

static void Check(bool ok, const char *what) {
  if (!ok) { std::cerr << "FAIL: " << what << '\n'; std::exit(1); }
}

static std::vector<OfflineCtcDecoderResult> Decode(
    std::vector<float> data, int64_t batch, int64_t frames, int64_t vocab,
    std::vector<int64_t> lengths, int beam = 128, float score = 0,
    const std::vector<std::vector<int32_t>> &words = {}) {
  auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  int64_t shape[] = {batch, frames, vocab};
  auto x = Ort::Value::CreateTensor<float>(memory, data.data(), data.size(), shape, 3);
  auto len = Ort::Value::CreateTensor<int64_t>(memory, lengths.data(), lengths.size(), &batch, 1);
  OfflineSenseVoiceCtcBeamSearchDecoder decoder(0, beam, score, words);
  return decoder.Decode(std::move(x), std::move(len));
}

static std::vector<float> WithMetadata(const std::vector<float> &text, int vocab) {
  std::vector<float> data(4 * vocab, -100);
  // Deliberately include repeated metadata and blank: copy four FRAMES.
  for (int t = 0; t < 4; ++t) data[t * vocab + (t == 2 ? 0 : 1)] = 0;
  data.insert(data.end(), text.begin(), text.end());
  return data;
}

static void Exhaustive() {
  std::mt19937 rng(20260505);
  std::uniform_real_distribution<float> logit(-4, 0);
  for (int trial = 0; trial < 300; ++trial) {
    const int vocab = 3, frames = 5;
    std::vector<float> text(frames * vocab);
    for (auto &x : text) x = logit(rng);
    std::map<std::vector<int64_t>, double> sums;
    std::map<std::vector<int64_t>, double> best;
    std::map<std::vector<int64_t>, std::vector<int32_t>> times;
    std::function<void(int, int, double, std::vector<int64_t>, std::vector<int32_t>)> enumerate;
    enumerate = [&](int t, int last, double p, std::vector<int64_t> seq,
                    std::vector<int32_t> ts) {
      if (t == frames) {
        sums[seq] += p;
        if (p > best[seq]) { best[seq] = p; times[seq] = ts; }
        return;
      }
      for (int id = 0; id < vocab; ++id) {
        auto s = seq; auto z = ts;
        if (id != 0 && id != last) { s.push_back(id); z.push_back(t + 4); }
        enumerate(t + 1, id, p * std::exp(text[t * vocab + id]), s, z);
      }
    };
    enumerate(0, -1, 1, {}, {});
    for (int mode = 0; mode < 3; ++mode) {
      std::vector<std::vector<int32_t>> words;
      if (mode) words = {{1, 2}, {2}, {1, 1}, {1, 2, 1}};
      double score = mode == 2 ? 1.7 : 0;
      // Independent oracle: completed occurrences only, including overlaps
      // and suffix matches. No ContextGraph is used by the expected result.
      auto ranked = sums;
      for (auto &entry : ranked) {
        int rewarded_tokens = 0;
        for (const auto &word : words)
          for (size_t i = 0; i + word.size() <= entry.first.size(); ++i)
            if (std::equal(word.begin(), word.end(), entry.first.begin() + i))
              rewarded_tokens += word.size();
        entry.second *= std::exp(score * rewarded_tokens);
      }
      auto winner = std::max_element(ranked.begin(), ranked.end(),
          [](const auto &a, const auto &b) { return a.second < b.second; });
      auto r = Decode(WithMetadata(text, vocab), 1, frames + 4, vocab,
                      {frames + 4}, 128, score, words)[0];
      std::vector<int64_t> output(r.tokens.begin() + 4, r.tokens.end());
      Check(output == winner->first, "exhaustive CTC/context mass (blank/repeat/merge/overlap/refund)");
      Check(std::vector<int32_t>(r.timestamps.begin() + 4, r.timestamps.end()) == times[output], "Viterbi representative timestamps");
      Check(std::vector<int64_t>(r.tokens.begin(), r.tokens.begin() + 4) == std::vector<int64_t>({1, 1, 0, 1}), "four metadata frames preserved");
      Check(r.tokens.size() == r.timestamps.size(), "timestamps length");
    }
  }
}

static void Hotwords() {
  // A wins acoustic-only; AB hotword promotes B despite B outside top K.
  auto text = WithMetadata({-20, 0, -20, -20, 0, -2}, 3);
  auto plain = Decode(text, 1, 6, 3, {6}, 1)[0];
  auto biased = Decode(text, 1, 6, 3, {6}, 1, 2, {{1, 2}})[0];
  Check(plain.tokens.size() == 5 && plain.tokens.back() == 1, "unbiased repeat collapse");
  Check(biased.tokens == std::vector<int64_t>({1, 1, 0, 1, 1, 2}), "hotword completion and forced candidates");
  // Partial prefix receives temporary reward but finalization refunds it.
  auto partial = WithMetadata({-20, -1, 0}, 3);
  auto r = Decode(partial, 1, 5, 3, {5}, 8, 20, {{1, 2}})[0];
  Check(r.tokens.back() == 2, "partial hotword finalization refunds reward before last prune");
  // Completed phrase retains reward; no duplicate reward for CTC self-loops.
  auto repeat = Decode(WithMetadata({-20, 0, -3, -20, 0, -3}, 3), 1, 6, 3, {6}, 8, 2, {{1}})[0];
  Check(repeat.tokens.size() == 5, "hotword not rewarded on repeated CTC frames");
  // Incomplete phrase at the end of a longer utterance cannot beat acoustics.
  auto long_partial = Decode(WithMetadata({-20, -1, 0, 0, -20, -20}, 3), 1, 6, 3, {6}, 128, 20, {{1, 1}})[0];
  Check(long_partial.tokens.back() == 2, "partial refund after blank");
}

static void Batch() {
  auto one = WithMetadata({-20, 0, -20, 0, -20, -20, -20, 0, -20}, 3);
  auto two = WithMetadata({-20, -20, 0, -20, -20, 0, -20, 0, -20}, 3);
  auto combined = one; combined.insert(combined.end(), two.begin(), two.end());
  auto batch = Decode(combined, 2, 7, 3, {7, 5});
  Check(batch[0].tokens == std::vector<int64_t>({1, 1, 0, 1, 1, 1}), "blank-separated repeated token");
  Check(batch[0].timestamps == std::vector<int32_t>({0, 1, 2, 3, 4, 6}), "repeat timestamps");
  Check(batch[1].tokens == std::vector<int64_t>({1, 1, 0, 1, 2}), "batch length excludes padding");
  auto metadata = Decode(WithMetadata({}, 3), 1, 4, 3, {4})[0];
  Check(metadata.tokens.size() == 4, "metadata-only output");
  bool threw = false;
  try { Decode(one, 1, 7, 3, {8}); } catch (const std::invalid_argument &) { threw = true; }
  Check(threw, "invalid batch length rejected");
}

static void Parser() {
  sherpa_onnx::SymbolTable symbols("<blk> 0\n轴 1\n伊 2\n<unk> 3\n<|zh|> 4\n<s> 5\n[eos] 6\n", false);
  std::vector<std::vector<int32_t>> words;
  std::istringstream good("轴 伊\r\n\n伊\n");
  Check(sherpa_onnx::ReadJpetSenseVoiceHotwords(good, symbols, 0, &words), "UTF-8 token parser");
  Check(words == std::vector<std::vector<int32_t>>({{1, 2}, {2}}), "strict whitespace-tokenized phrases");
  for (const char *line : {"轴伊", "<blk>", "<unk>", "<|zh|>", "<s>", "[eos]", "轴 :2"}) {
    std::istringstream bad(line);
    Check(!sherpa_onnx::ReadJpetSenseVoiceHotwords(bad, symbols, 0, &words), "invalid hotword rejected");
  }
  for (int beam : {0, 129}) {
    bool threw = false;
    try { OfflineSenseVoiceCtcBeamSearchDecoder d(0, beam, 0); } catch (const std::invalid_argument &) { threw = true; }
    Check(threw, "beam bounds");
  }
  for (float score : {-1.0f, 101.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
    bool threw = false;
    try { OfflineSenseVoiceCtcBeamSearchDecoder d(0, 8, score); } catch (const std::invalid_argument &) { threw = true; }
    Check(threw, "score bounds");
  }
}

int main() {
  Exhaustive(); Hotwords(); Batch(); Parser();
  std::cout << "voice_ctc_decoder_test: PASS (900 exhaustive decodes, hotwords, metadata, batch, parser, bounds)\n";
}
