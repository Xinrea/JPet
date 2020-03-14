#include <thread>

class UserStateManager
{
public:
    void Init(int roomID, int time);
    void Start();
    void Stop();
};
