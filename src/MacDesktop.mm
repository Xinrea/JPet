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

void InstallEditMenu() {
  // GLFW creates the application and Window menus, but no editing commands.
  // WKWebView relies on these key equivalents and the Cocoa responder chain.
  NSMenu* edit = [[NSMenu alloc] initWithTitle:@"编辑"];
  [edit addItemWithTitle:@"撤销" action:@selector(undo:) keyEquivalent:@"z"];
  NSMenuItem* redo = [edit addItemWithTitle:@"重做" action:@selector(redo:) keyEquivalent:@"z"];
  redo.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
  [edit addItem:NSMenuItem.separatorItem];
  [edit addItemWithTitle:@"剪切" action:@selector(cut:) keyEquivalent:@"x"];
  [edit addItemWithTitle:@"复制" action:@selector(copy:) keyEquivalent:@"c"];
  [edit addItemWithTitle:@"粘贴" action:@selector(paste:) keyEquivalent:@"v"];
  [edit addItemWithTitle:@"全选" action:@selector(selectAll:) keyEquivalent:@"a"];
  // Leave targets nil so commands reach the focused WebView or native field.
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:edit.title action:nil keyEquivalent:@""];
  item.submenu = edit;
  [NSApp.mainMenu insertItem:item atIndex:1];
}
}

void MacDesktop::Initialize(GLFWwindow* window) {
  InstallEditMenu();
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
  NSImage* trayIcon = [NSImage imageNamed:@"tray"];
  if (trayIcon) {
    [trayIcon setTemplate:YES];  // `template` is a C++ keyword in Objective-C++.
    trayIcon.accessibilityDescription = @"JPet";
    statusItem.button.image = trayIcon;
    statusItem.button.toolTip = @"JPet";
  } else {
    statusItem.button.title = @"JPet";
  }
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
