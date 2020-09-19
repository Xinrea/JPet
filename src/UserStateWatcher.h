#include <thread>
#include <vector>
#include <string>
#include <queue>
#include "LAppPal.hpp"
#include "StateMessage.hpp"

using std::vector;
using std::string;
using std::queue;

class UserStateWatcher
{
public:
	bool isExit = false;
	vector<bool> lastLive;
	vector<long long> lastDynamic;
	queue<StateMessage>  newLive;
	queue<StateMessage>  newDynamic;
	vector<string> uidlist;
	UserStateWatcher(string list)
	{
		uidlist = split(list, ";");
		for (int i = 0; i < uidlist.size(); i++)
		{
			lastDynamic.push_back(0);
			lastLive.push_back(false);
		}
	}
    void Watch();
	static bool CheckUpdate();
	std::thread WatchThread();

	vector<string> split(const string& str, const string& delim) {
		vector<string> res;
		if ("" == str) return res;
		//先将要切割的字符串从string类型转换为char*类型
		char* strs = new char[str.length() + 1]; //不要忘了
		strcpy(strs, str.c_str());

		char* d = new char[delim.length() + 1];
		strcpy(d, delim.c_str());

		char* p = strtok(strs, d);
		while (p) {
			string s = p; //分割得到的字符串转换为string类型
			res.push_back(s); //存入结果数组
			p = strtok(NULL, d);
		}

		return res;
	};

private:
	wstring StringToWString(const std::string& str);
};