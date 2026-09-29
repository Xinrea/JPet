#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

#include "GamePanel.hpp"
#include "CookieWindow.hpp"
#include "Platform.hpp"

#include <mutex>
#include <functional>
#include <algorithm>
#include <cstdlib>

namespace {
// Every queued operation owns its state; no block captures the C++ window.
void OnMain(std::function<void()> action) {
  if ([NSThread isMainThread]) action();
  else dispatch_async(dispatch_get_main_queue(), ^{ action(); });
}

struct BrowserState {
  NSWindow* window = nil;
  WKWebView* view = nil;
  id delegate = nil;
  std::mutex mutex;
  std::string cookies;
  std::string userAgent;
  bool visible = false;
};

bool IsBilibili(NSString* host) {
  return [host isEqualToString:@"bilibili.com"] ||
         [host hasSuffix:@".bilibili.com"];
}

void ReadCookies(const std::shared_ptr<BrowserState>& state) {
  const auto keepAlive = state;
  [state->view.configuration.websiteDataStore.httpCookieStore
      getAllCookies:^(NSArray<NSHTTPCookie*>* cookies) {
    std::string result;
    for (NSHTTPCookie* cookie in cookies) {
      NSString* domain = cookie.domain;
      if ([domain hasPrefix:@"."]) domain = [domain substringFromIndex:1];
      if (!IsBilibili(domain)) continue;
      if (!result.empty()) result += "; ";
      result += cookie.name.UTF8String;
      result += "=";
      result += cookie.value.UTF8String;
    }
    std::lock_guard<std::mutex> lock(keepAlive->mutex);
    keepAlive->cookies = std::move(result);
  }];
}
}

@interface JPetBrowserDelegate : NSObject <NSWindowDelegate, WKNavigationDelegate,
                                           WKUIDelegate, WKHTTPCookieStoreObserver> {
 @public
  std::weak_ptr<BrowserState> state;
  BOOL login;
}
@end

@implementation JPetBrowserDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender {
  [sender orderOut:nil];
  if (auto value = state.lock()) {
    std::lock_guard<std::mutex> lock(value->mutex);
    value->visible = false;
  }
  return NO;
}
- (void)cookiesDidChangeInCookieStore:(WKHTTPCookieStore*)store {
  if (auto value = state.lock()) ReadCookies(value);
}
- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
  NSLog(@"JPet WebView loaded: %@", webView.URL.absoluteString);
  if (!login) return;
  if (auto value = state.lock()) {
    const auto keepAlive = value;
    ReadCookies(value);
    [webView evaluateJavaScript:@"navigator.userAgent"
             completionHandler:^(id result, NSError* error) {
      (void)error;
      if ([result isKindOfClass:[NSString class]]) {
        std::lock_guard<std::mutex> lock(keepAlive->mutex);
        keepAlive->userAgent = [(NSString*)result UTF8String];
      }
    }];
  }
}
- (void)webView:(WKWebView*)webView didFailNavigation:(WKNavigation*)navigation
       withError:(NSError*)error {
  NSLog(@"JPet settings navigation failed: %@", error);
}
- (void)webView:(WKWebView*)webView
    didFailProvisionalNavigation:(WKNavigation*)navigation
       withError:(NSError*)error {
  NSLog(@"JPet settings provisional navigation failed: %@", error);
}
- (void)webViewWebContentProcessDidTerminate:(WKWebView*)webView {
  NSLog(@"JPet WebKit content process terminated");
  [webView reload];
}
- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)action
    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
  NSURL* url = action.request.URL;
  BOOL http = [url.scheme isEqualToString:@"https"] ||
              [url.scheme isEqualToString:@"http"];
  BOOL internal = login ? IsBilibili(url.host) :
      (([url.host isEqualToString:@"localhost"] ||
        [url.host isEqualToString:@"127.0.0.1"]) && url.port.integerValue == 8053);
  if (http && internal) {
    decisionHandler(WKNavigationActionPolicyAllow);
  } else {
    if (http && action.navigationType == WKNavigationTypeLinkActivated)
      Platform::Open(url.absoluteString.UTF8String);
    decisionHandler(WKNavigationActionPolicyCancel);
  }
}
- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
    forNavigationAction:(WKNavigationAction*)action
    windowFeatures:(WKWindowFeatures*)features {
  if (!action.targetFrame) {
    NSURL* url = action.request.URL;
    if (login && IsBilibili(url.host)) [webView loadRequest:action.request];
    else if ([url.scheme isEqualToString:@"https"] || [url.scheme isEqualToString:@"http"])
      Platform::Open(url.absoluteString.UTF8String);
  }
  return nil;
}
- (void)webView:(WKWebView*)webView runJavaScriptAlertPanelWithMessage:(NSString*)message
    initiatedByFrame:(WKFrameInfo*)frame completionHandler:(void (^)(void))completionHandler {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = message;
  [alert beginSheetModalForWindow:webView.window completionHandler:^(NSModalResponse response) {
    (void)response;
    completionHandler();
  }];
}
- (void)webView:(WKWebView*)webView runJavaScriptConfirmPanelWithMessage:(NSString*)message
    initiatedByFrame:(WKFrameInfo*)frame completionHandler:(void (^)(BOOL))completionHandler {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = message;
  [alert addButtonWithTitle:@"确定"];
  [alert addButtonWithTitle:@"取消"];
  [alert beginSheetModalForWindow:webView.window completionHandler:^(NSModalResponse response) {
    completionHandler(response == NSAlertFirstButtonReturn);
  }];
}
@end

namespace {
void CreateBrowser(const std::shared_ptr<BrowserState>& state, bool login) {
  state->window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, 800, login ? 600 : 850)
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
      backing:NSBackingStoreBuffered defer:NO];
  state->window.releasedWhenClosed = NO;
  state->window.title = login ? @"哔哩哔哩登录" : @"JPet 设置面板";
  [state->window center];
  WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
  // The default store persists Bilibili sessions across app launches.
  config.websiteDataStore = [WKWebsiteDataStore defaultDataStore];
  state->view = [[WKWebView alloc] initWithFrame:state->window.contentView.bounds
                                 configuration:config];
  state->view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  JPetBrowserDelegate* delegate = [[JPetBrowserDelegate alloc] init];
  delegate->state = state;
  delegate->login = login;
  state->delegate = delegate; // WebKit and NSWindow delegate references are weak.
  state->window.delegate = delegate;
  state->view.navigationDelegate = delegate;
  state->view.UIDelegate = delegate;
  state->window.contentView = state->view;
  if (login) {
    [config.websiteDataStore.httpCookieStore addObserver:delegate];
    ReadCookies(state);
    // UA is available on the initial blank document even if login navigation
    // is slow or offline; do not make the watcher wait for a network response.
    const auto keepAlive = state;
    [state->view evaluateJavaScript:@"navigator.userAgent"
                 completionHandler:^(id result, NSError* error) {
      (void)error;
      if ([result isKindOfClass:[NSString class]]) {
        std::lock_guard<std::mutex> lock(keepAlive->mutex);
        keepAlive->userAgent = [(NSString*)result UTF8String];
      }
    }];
  }
  NSString* url = login ? @"https://space.bilibili.com/475210/dynamic" :
                          @"http://127.0.0.1:8053/index.html";
  if (!login || !std::getenv("JPET_SMOKE_TEST")) {
    NSURLRequest* request = [NSURLRequest requestWithURL:[NSURL URLWithString:url]];
    const auto keepAlive = state;
    // PanelServer registers its routes on a worker immediately after the
    // window is created. Delay the first request to avoid racing listen().
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                  login ? 0 : (int64_t)(0.25 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
      if (keepAlive->view && request) [keepAlive->view loadRequest:request];
    });
  }
}

void Dispose(const std::shared_ptr<BrowserState>& state) {
  OnMain([state] {
    [state->view.configuration.websiteDataStore.httpCookieStore removeObserver:state->delegate];
    [state->view stopLoading];
    state->view.navigationDelegate = nil;
    state->view.UIDelegate = nil;
    state->window.delegate = nil;
    [state->window close];
    state->view = nil;
    state->window = nil;
    state->delegate = nil;
  });
}

void SetVisible(const std::shared_ptr<BrowserState>& state, bool visible) {
  if (visible) {
    [NSApp activateIgnoringOtherApps:YES];
    [state->window makeKeyAndOrderFront:nil];
  } else [state->window orderOut:nil];
  std::lock_guard<std::mutex> lock(state->mutex);
  state->visible = visible;
}

void Load(const std::shared_ptr<BrowserState>& state, const std::string& url) {
  OnMain([state, url] {
    NSString* string = [NSString stringWithUTF8String:url.c_str()];
    NSURL* target = [NSURL URLWithString:string];
    if (target) [state->view loadRequest:[NSURLRequest requestWithURL:target]];
  });
}

void ResizeBrowser(const std::shared_ptr<BrowserState>& state, int width, int height) {
  OnMain([state, width, height] {
    [state->window setContentSize:NSMakeSize(std::max(1, width), std::max(1, height))];
  });
}
}

struct GamePanel::Impl : BrowserState {};
struct CookieWindow::Impl : BrowserState {};

GamePanel::GamePanel(void*) : _impl(std::make_shared<Impl>()) {
  OnMain([state = _impl] { CreateBrowser(state, false); });
}
GamePanel::~GamePanel() { Dispose(_impl); }
void GamePanel::Navigate(const std::string& url) { Load(_impl, url); }
void GamePanel::Resize(int width, int height) { ResizeBrowser(_impl, width, height); }
void GamePanel::Show() {
  OnMain([state = _impl] { SetVisible(state, !state->window.visible); });
}
void GamePanel::ForceShow() {
  OnMain([state = _impl] { SetVisible(state, true); });
}
void GamePanel::Close() {
  OnMain([state = _impl] { SetVisible(state, false); });
}

CookieWindow::CookieWindow(void*) : _impl(std::make_shared<Impl>()) {
  OnMain([state = _impl] { CreateBrowser(state, true); });
}
CookieWindow::~CookieWindow() { Dispose(_impl); }
void CookieWindow::Navigate(const std::string& url) { Load(_impl, url); }
void CookieWindow::Resize(int width, int height) { ResizeBrowser(_impl, width, height); }
void CookieWindow::Reload() {
  OnMain([state = _impl] { [state->view reload]; ReadCookies(state); });
}
void CookieWindow::doReload() { Reload(); }
void CookieWindow::UpdateCookie() { Reload(); }
void CookieWindow::Show() {
  OnMain([state = _impl] { [state->view reload]; SetVisible(state, true); });
}
void CookieWindow::Hide() {
  OnMain([state = _impl] { SetVisible(state, false); });
}
bool CookieWindow::IsVisible() {
  std::lock_guard<std::mutex> lock(_impl->mutex);
  return _impl->visible;
}
std::string CookieWindow::GetUserAgent() const {
  std::lock_guard<std::mutex> lock(_impl->mutex);
  return _impl->userAgent;
}
std::string CookieWindow::GetCookies() const {
  std::lock_guard<std::mutex> lock(_impl->mutex);
  return _impl->cookies;
}
