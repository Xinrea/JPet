#include <thread>

class UserStateWatcher
{
public:
	bool isLive = false;
	bool isNewDynamic = false;
	bool isNewFollow = false;
	bool isExit = false;
    void Watch();
	static bool CheckUpdate();
	std::thread WatchThread();
};