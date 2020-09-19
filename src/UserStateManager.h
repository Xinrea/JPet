#include <thread>
#include <queue>
#include "StateMessage.hpp"
#include "UserStateWatcher.h"
using std::queue;

class UserStateManager
{
public:
    void Init(std::string list)
    {
        if (_Watcher) Stop();
        _Watcher = new UserStateWatcher(list);
        std::thread t = _Watcher->WatchThread();
        t.detach();
    }
	void Stop()
    {
        _Watcher->isExit = true;
    }
    queue<StateMessage>& GetLiveState()
    {
        return _Watcher->newLive;
    }
    queue<StateMessage>& GetDynamicState()
    {
        return _Watcher->newDynamic;
    }
    bool CheckUpdate()
    {
        return UserStateWatcher::CheckUpdate();
    }
private:
    UserStateWatcher* _Watcher;
};
