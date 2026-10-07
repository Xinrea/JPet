#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>

struct CloudSocketEvent {
  enum class Type { Message, Error } type = Type::Message;
  std::string text;
};

// Platform sockets receive in the background; CloudGame alone applies snapshots.
class CloudSocket {
 public:
  virtual ~CloudSocket() = default;
  virtual void Connect(const std::string& url, std::function<void()> notify) = 0;
  virtual bool Send(const std::string& message) = 0;
  virtual bool Receive(CloudSocketEvent& event, std::chrono::milliseconds timeout) = 0;
  virtual void Close() = 0;
};

std::unique_ptr<CloudSocket> CreateCloudSocket();
