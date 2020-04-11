#include <thread>
#include "UserStateWatcher.h"

class UserStateManager
{
public:
    void Init()
    {
        _Watcher = new UserStateWatcher();
        std::thread t = _Watcher->WatchThread();
        t.detach();
    }
	void Stop()
    {
        _Watcher->isExit = true;
    }
	bool GetLiveState()
    {
        return _Watcher->isLive;
    }
    bool GetDynamicState()
    {
        return _Watcher->isNewDynamic;
    }
    void SetDynamicState()
    {
        _Watcher->isNewDynamic = false;
    }
    bool GetFollowState()
    {
        return _Watcher->isNewFollow;
    }
    void SetFollowState()
    {
        _Watcher->isNewFollow = false;
    }
    bool CheckUpdate()
    {
        return UserStateWatcher::CheckUpdate();
    }
private:
    UserStateWatcher* _Watcher;
};
