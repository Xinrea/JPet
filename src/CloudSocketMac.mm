#include "CloudSocketInbox.hpp"
#include <atomic>
#import <Foundation/Foundation.h>

namespace {
struct MacConnection : std::enable_shared_from_this<MacConnection> {
  explicit MacConnection(std::function<void()> notify) : inbox(std::move(notify)) {}
  NSURLSession* __strong session = nil;
  NSURLSessionWebSocketTask* __strong task = nil;
  CloudSocketInbox inbox;
  std::atomic<bool> stopping{false};

  void Receive() {
    auto self = shared_from_this();
    [task receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage* message, NSError* error) {
      if (self->stopping) return;
      if (error || !message) {
        self->inbox.Push({CloudSocketEvent::Type::Error, "云端连接中断，游戏已暂停，正在重试"});
        return;
      }
      if (message.type != NSURLSessionWebSocketMessageTypeString || !message.string) {
        self->inbox.Push({CloudSocketEvent::Type::Error, "云端返回了无效消息，正在重新连接"});
        return;
      }
      NSData* data = [message.string dataUsingEncoding:NSUTF8StringEncoding];
      self->inbox.Push({CloudSocketEvent::Type::Message,
        std::string(static_cast<const char*>(data.bytes), data.length)});
      self->Receive();
    }];
  }
};

class MacCloudSocket final : public CloudSocket {
 public:
  ~MacCloudSocket() override { Close(); }
  void Connect(const std::string& url, std::function<void()> notify) override {
    Close();
    @autoreleasepool {
      connection_ = std::make_shared<MacConnection>(std::move(notify));
      NSString* text = [[NSString alloc] initWithBytes:url.data() length:url.size() encoding:NSUTF8StringEncoding];
      NSURLSessionConfiguration* config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
      config.timeoutIntervalForRequest = 8;
      connection_->session = [NSURLSession sessionWithConfiguration:config];
      connection_->task = [connection_->session webSocketTaskWithURL:[NSURL URLWithString:text]];
      connection_->task.maximumMessageSize = CloudSocketInbox::MaxMessageBytes;
      [connection_->task resume];
      connection_->Receive();
    }
  }
  bool Send(const std::string& message) override {
    if (!connection_ || connection_->stopping) return false;
    @autoreleasepool {
      auto state = connection_;
      NSString* text = [[NSString alloc] initWithBytes:message.data() length:message.size() encoding:NSUTF8StringEncoding];
      if (!text) return false;
      [state->task sendMessage:[[NSURLSessionWebSocketMessage alloc] initWithString:text] completionHandler:^(NSError* error) {
        if (error && !state->stopping) state->inbox.Push({CloudSocketEvent::Type::Error, "云端消息发送失败，正在重新连接"});
      }];
      return true;
    }
  }
  bool Receive(CloudSocketEvent& event, std::chrono::milliseconds timeout) override {
    return connection_ && connection_->inbox.Receive(event, timeout);
  }
  void Close() override {
    if (!connection_) return;
    @autoreleasepool {
      connection_->stopping = true;
      connection_->inbox.Close();
      [connection_->task cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
      [connection_->session invalidateAndCancel];
      connection_.reset();
    }
  }
 private:
  std::shared_ptr<MacConnection> connection_;
};
}

std::unique_ptr<CloudSocket> CreateCloudSocket() { return std::make_unique<MacCloudSocket>(); }
