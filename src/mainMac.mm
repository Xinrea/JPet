#import <AppKit/AppKit.h>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

#include "LAppDefine.hpp"
#include "LAppDelegate.hpp"
#include "LAppPal.hpp"

int main(int argc, char** argv) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    const bool smokeTest = argc == 2 && std::strcmp(argv[1], "--smoke-test") == 0;
    if (smokeTest) setenv("JPET_SMOKE_TEST", "1", 1);

    NSString* resources = NSBundle.mainBundle.resourcePath;
    if (![[NSFileManager defaultManager] fileExistsAtPath:[resources stringByAppendingPathComponent:@"resources"]]) {
      std::cerr << "JPet resources missing. Build and launch JPet.app.\n";
      return 1;
    }
    std::filesystem::current_path(resources.fileSystemRepresentation);
    LAppDefine::execPath = LAppPal::StringToWString(resources.UTF8String) + L"/";

    const char* overridePath = std::getenv("JPET_DATA_DIR");
    if (smokeTest && (!overridePath || !*overridePath)) {
      std::cerr << "--smoke-test requires JPET_DATA_DIR pointing to an isolated test directory.\n";
      return 1;
    }
    NSString* data = overridePath ? [NSString stringWithUTF8String:overridePath] :
        [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject
            stringByAppendingPathComponent:@"JPet"];
    NSError* error = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:data
                                 withIntermediateDirectories:YES attributes:nil error:&error]) {
      NSLog(@"Cannot create JPet data directory: %@", error);
      return 1;
    }
    LAppDefine::documentPath = LAppPal::StringToWString(data.UTF8String);
    NSString* lockPath = [data stringByAppendingPathComponent:@".lock"];
    const int lock = open(lockPath.fileSystemRepresentation, O_CREAT | O_RDWR, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) != 0) {
      if (lock >= 0) close(lock);
      NSLog(@"JPet is already running, or its data directory cannot be locked.");
      return 1;
    }

    LAppPal::Init();
    int result = 0;
    try {
      if (!LAppDelegate::GetInstance()->Initialize()) {
        result = 1;
      } else {
        if (smokeTest) {
          dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC),
                         dispatch_get_main_queue(), ^{
            LAppDelegate::GetInstance()->AppEnd();
          });
        }
        LAppDelegate::GetInstance()->Run();
      }
    } catch (const std::exception& e) {
      std::cerr << "JPet startup failed: " << e.what() << '\n';
      result = 1;
    }
    LAppDelegate::ReleaseInstance();
    LAppPal::ReleaseLog();
    flock(lock, LOCK_UN);
    close(lock);
    return result;
  }
}
