#pragma once

// A tiny observation API shared by the fake dynamic runtime and its test driver.
// The production wrapper does not know about these functions or this header.
struct VoiceSpeechMockState {
  int capability = 0;
  int created = 0;
  int destroyed = 0;
  int streams = 0;
  int streamsDestroyed = 0;
  int accepted = 0;
  int decoded = 0;
  int results = 0;
  int resultsDestroyed = 0;
  char failure[256] = {};
};

enum VoiceSpeechMockMode { Normal, RejectRecognizer, RejectStream, NullResult, NullText };
using VoiceSpeechMockConfigure = void (*)(const char* modelDirectory, const char* hotwords, int mode);
using VoiceSpeechMockInspect = const VoiceSpeechMockState* (*)();
