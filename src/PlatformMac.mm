#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>

#include "Platform.hpp"
#include "LAppDelegate.hpp"
#include "LAppPal.hpp"
#include "PanelServer.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <mutex>

namespace {
NSString* Text(const std::wstring& text) {
  return [NSString stringWithUTF8String:LAppPal::WStringToString(text).c_str()];
}

void OnMainSync(dispatch_block_t block) {
  if ([NSThread isMainThread]) block();
  else dispatch_sync(dispatch_get_main_queue(), block);
}
}

@interface JPetNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation JPetNotificationDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
      willPresentNotification:(UNNotification*)notification
        withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completion {
  completion(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionSound);
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
 didReceiveNotificationResponse:(UNNotificationResponse*)response
         withCompletionHandler:(void (^)(void))completion {
  NSString* action = response.notification.request.content.userInfo[@"action"];
  if ([action isEqualToString:@"TASK_COMPLETE"] || [action isEqualToString:@"SOFTWARE_UPDATE"]) {
    dispatch_async(dispatch_get_main_queue(), ^{
      LAppDelegate::GetInstance()->ForceShowPanel();
      PanelServer::GetInstance()->Notify(action.UTF8String);
    });
  } else if (action.length) {
    Platform::Open(action.UTF8String);
  }
  completion();
}
@end

void Platform::Open(const std::string& pathOrURL) {
  NSString* value = [NSString stringWithUTF8String:pathOrURL.c_str()];
  dispatch_async(dispatch_get_main_queue(), ^{
    NSURL* url = [value hasPrefix:@"/"] ? [NSURL fileURLWithPath:value] : [NSURL URLWithString:value];
    if (url) [[NSWorkspace sharedWorkspace] openURL:url];
  });
}

bool Platform::OpenWebURL(const std::string& address, std::string& error) {
  @autoreleasepool {
    NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:address.c_str()]];
    NSString* scheme = url.scheme.lowercaseString;
    if (!url.host.length || (!([scheme isEqualToString:@"http"] || [scheme isEqualToString:@"https"]))) {
      error = "网页地址格式不正确";
      return false;
    }
    struct Request {
      std::mutex mutex;
      std::condition_variable completed;
      bool done = false, abandoned = false, opened = false;
    };
    auto request = std::make_shared<Request>();
    dispatch_block_t launch = ^{
      {
        std::lock_guard<std::mutex> lock(request->mutex);
        if (request->abandoned) return;
      }
      const bool opened = [[NSWorkspace sharedWorkspace] openURL:url];
      {
        std::lock_guard<std::mutex> lock(request->mutex);
        request->opened = opened;
        request->done = true;
      }
      request->completed.notify_one();
    };
    if ([NSThread isMainThread]) launch();
    else dispatch_async(dispatch_get_main_queue(), launch);
    std::unique_lock<std::mutex> lock(request->mutex);
    // Stop() joins the tool worker on the main thread. A bounded wait prevents
    // a deadlock and prevents a queued launch from opening after shutdown.
    if (!request->completed.wait_for(lock, std::chrono::seconds(5), [&] { return request->done; })) {
      request->abandoned = true;
      error = "浏览器打开请求超时，请检查浏览器后重试";
      return false;
    }
    if (!request->opened) error = "无法启动系统默认浏览器，请检查默认浏览器设置";
    return request->opened;
  }
}

void Platform::Alert(const std::wstring& title, const std::wstring& message) {
  NSString* heading = Text(title);
  NSString* body = Text(message);
  dispatch_async(dispatch_get_main_queue(), ^{
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = heading;
    alert.informativeText = body;
    [alert runModal];
  });
}

void Platform::Notify(const std::wstring& title, const std::wstring& message,
                      const std::string& url) {
  // Notification authorization is requested only when there is a notification
  // to deliver, not on every launch. Smoke tests must never prompt the user.
  if (std::getenv("JPET_SMOKE_TEST")) return;
  NSString* heading = Text(title);
  NSString* body = Text(message);
  NSString* action = [NSString stringWithUTF8String:url.c_str()];
  dispatch_async(dispatch_get_main_queue(), ^{
    static JPetNotificationDelegate* delegate = [[JPetNotificationDelegate alloc] init];
    UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
    center.delegate = delegate;
    [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                         completionHandler:^(BOOL granted, NSError* error) {
      if (!granted) return;
      UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
      content.title = heading;
      content.body = body;
      content.userInfo = @{@"action": action ?: @""};
      content.sound = [UNNotificationSound defaultSound];
      UNNotificationRequest* request = [UNNotificationRequest
          requestWithIdentifier:NSUUID.UUID.UUIDString content:content trigger:nil];
      [center addNotificationRequest:request withCompletionHandler:^(NSError* deliveryError) {
        if (deliveryError) NSLog(@"JPet notification: %@", deliveryError);
      }];
    }];
  });
}

bool Platform::Browse(std::wstring& path, bool directory) {
  __block NSString* selected = nil;
  OnMainSync(^{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = directory;
    panel.canChooseFiles = !directory;
    panel.allowsMultipleSelection = NO;
    panel.treatsFilePackagesAsDirectories = NO;
    if ([panel runModal] == NSModalResponseOK) selected = panel.URL.path;
  });
  if (!selected) return false;
  path = LAppPal::StringToWString(selected.UTF8String);
  return true;
}

bool Platform::SaveFile(const std::wstring& source, const std::wstring& suggestedName) {
  NSString* sourcePath = Text(source);
  NSString* name = Text(suggestedName);
  __block BOOL saved = NO;
  OnMainSync(^{
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.nameFieldStringValue = name;
    if ([panel runModal] != NSModalResponseOK) return;
    NSError* error = nil;
    // NSData's atomic write safely replaces a destination confirmed by the user.
    NSData* contents = [NSData dataWithContentsOfFile:sourcePath options:0 error:&error];
    if (contents) saved = [contents writeToURL:panel.URL options:NSDataWritingAtomic error:&error];
    if (error) NSLog(@"JPet save: %@", error);
  });
  return saved;
}

bool Platform::TrashFile(const std::wstring& path) {
  @autoreleasepool {
    NSError* error = nil;
    BOOL result = [[NSFileManager defaultManager] trashItemAtURL:[NSURL fileURLWithPath:Text(path)]
                                             resultingItemURL:nil error:&error];
    if (error) NSLog(@"JPet trash: %@", error);
    return result;
  }
}

bool Platform::ExtractUpdate(const std::filesystem::path& archive,
                             const std::filesystem::path& stage, std::string& error) {
  @autoreleasepool {
    NSTask* task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/ditto"];
    task.arguments = @[@"-x", @"-k", [NSString stringWithUTF8String:archive.c_str()],
                       [NSString stringWithUTF8String:stage.c_str()]];
    task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
    task.standardError = [NSFileHandle fileHandleWithNullDevice];
    NSError* launchError = nil;
    if (![task launchAndReturnError:&launchError]) { error = "无法启动更新包解压工具"; return false; }
    [task waitUntilExit];
    if (task.terminationStatus != 0) { error = "更新包解压失败，请重新下载"; return false; }
    return true;
  }
}

bool Platform::LaunchUpdate(const std::filesystem::path& staged,
                            const std::filesystem::path& work,
                            const std::filesystem::path& failureLog, std::string& error) {
  @autoreleasepool {
    const auto target = std::filesystem::path(NSBundle.mainBundle.bundlePath.fileSystemRepresentation);
    if (target.extension() != ".app" || target.string().find("/AppTranslocation/") != std::string::npos) {
      error = "请先将 JPet.app 移到应用程序目录，再重启 JPet 后更新"; return false;
    }
    const auto probe = target.parent_path() / (".jpet-write-test-" + work.filename().string());
    const int descriptor = open(probe.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (descriptor < 0) { error = "应用目录不可写，请移动 JPet.app 到当前用户的应用程序目录后重试"; return false; }
    close(descriptor); unlink(probe.c_str());
    try {
      const auto source = target / "Contents/Resources/resources/updater/macos.sh";
      const auto script = work / "updater.sh";
      std::filesystem::copy_file(source, script, std::filesystem::copy_options::overwrite_existing);
      const auto log = work / "updater.log";
      [[NSFileManager defaultManager] createFileAtPath:[NSString stringWithUTF8String:log.c_str()] contents:nil attributes:nil];
      NSTask* task = [[NSTask alloc] init];
      task.executableURL = [NSURL fileURLWithPath:@"/bin/bash"];
      task.arguments = @[[NSString stringWithUTF8String:script.c_str()],
                         [NSString stringWithFormat:@"%d", getpid()],
                         [NSString stringWithUTF8String:staged.c_str()],
                         [NSString stringWithUTF8String:target.c_str()],
                         [NSString stringWithUTF8String:failureLog.c_str()]];
      task.currentDirectoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:work.c_str()]];
      task.standardInput = [NSFileHandle fileHandleWithNullDevice];
      task.standardOutput = [NSFileHandle fileHandleForWritingAtPath:[NSString stringWithUTF8String:log.c_str()]];
      task.standardError = task.standardOutput;
      NSError* launchError = nil;
      if (![task launchAndReturnError:&launchError]) { error = "无法启动更新助手"; return false; }
      return true;
    } catch (const std::exception&) { error = "无法准备更新助手，请检查磁盘空间"; return false; }
  }
}
