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
		//�Ƚ�Ҫ�и���ַ�����string����ת��Ϊchar*����
		char* strs = new char[str.length() + 1]; //��Ҫ����
		strcpy(strs, str.c_str());

		char* d = new char[delim.length() + 1];
		strcpy(d, delim.c_str());

		char* p = strtok(strs, d);
		while (p) {
			string s = p; //�ָ�õ����ַ���ת��Ϊstring����
			res.push_back(s); //����������
			p = strtok(NULL, d);
		}

		return res;
	};

private:
	wstring StringToWString(const std::string& str);
};