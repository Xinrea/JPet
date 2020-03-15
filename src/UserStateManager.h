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
	bool GetState()
    {
        return _Watcher->isLive;
    }
private:
    UserStateWatcher* _Watcher;
};
