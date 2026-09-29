#import <AppKit/AppKit.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include "LAppDelegate.hpp"
#include "MacDesktop.hpp"

@interface JPetMenuActions : NSObject
@property(nonatomic, assign) GLFWwindow* petWindow;
- (void)performAction:(NSMenuItem*)sender;
@end

@implementation JPetMenuActions
- (void)performAction:(NSMenuItem*)sender {
  LAppDelegate::GetInstance()->OnTrayClickCallBack(self.petWindow, 0, sender.tag);
}
@end

namespace {
NSStatusItem* statusItem;
JPetMenuActions* menuActions;
}

void MacDesktop::Initialize(GLFWwindow* window) {
  NSWindow* native = glfwGetCocoaWindow(window);
  native.opaque = NO;
  native.backgroundColor = NSColor.clearColor;
  native.hasShadow = NO;
  // Transparent pixels may still intercept input. Keep the pet interactive.
  native.ignoresMouseEvents = NO;
  native.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                              NSWindowCollectionBehaviorFullScreenAuxiliary;
  menuActions = [[JPetMenuActions alloc] init];
  menuActions.petWindow = window;
  statusItem = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
  statusItem.button.title = @"JPet";
  NSMenu* menu = [[NSMenu alloc] init];
  NSArray<NSString*>* titles = @[@"显示 / 隐藏", @"设置", @"重置位置", @"项目主页", @"退出 JPet"];
  const NSInteger commands[] = {2004, 2001, 2002, 2005, 2003};
  for (NSUInteger i = 0; i < titles.count; ++i) {
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:titles[i]
        action:@selector(performAction:) keyEquivalent:@""];
    item.target = menuActions;
    item.tag = commands[i];
    [menu addItem:item];
  }
  statusItem.menu = menu;
}

void MacDesktop::Shutdown() {
  if (statusItem) [NSStatusBar.systemStatusBar removeStatusItem:statusItem];
  statusItem = nil;
  menuActions = nil;
}
