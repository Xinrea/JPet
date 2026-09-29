#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>

#include "Platform.hpp"
#include "LAppDelegate.hpp"
#include "LAppPal.hpp"
#include "PanelServer.hpp"

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
  if ([action isEqualToString:@"TASK_COMPLETE"]) {
    dispatch_async(dispatch_get_main_queue(), ^{
      LAppDelegate::GetInstance()->ForceShowPanel();
      PanelServer::GetInstance()->Notify("TASK_COMPLETE");
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
