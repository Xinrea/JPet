#include "GameTask.hpp"
#include "DataManager.hpp"
#include "LAppDefine.hpp"


#ifdef __APPLE__
#include "Platform.hpp"
#else
using namespace WinToastLib;
#endif

void GameTask::Notify(const wstring& title, const wstring& content,
                              WinToastEventHandler* handler) {
#ifdef __APPLE__
  std::unique_ptr<WinToastEventHandler> owned(handler);
  Platform::Notify(title, content, owned ? owned->GetUrl() : "");
#else
  WinToastTemplate templ = WinToastTemplate(WinToastTemplate::ImageAndText02);
  // convert char* to wstring
  templ.setTextField(title, WinToastTemplate::FirstLine);
  templ.setTextField(content, WinToastTemplate::SecondLine);
  std::wstring img = LAppDefine::execPath + std::wstring(L"resources/imgs/Avatar.png");
  templ.setImagePath(img);
  WinToast::instance()->showToast(templ, handler, nullptr);
#endif
}
