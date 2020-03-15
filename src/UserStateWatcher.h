#include <thread>

class UserStateWatcher
{
public:
	bool isLive = false;
	bool isExit = false;
    void Watch();
	std::thread WatchThread();
};