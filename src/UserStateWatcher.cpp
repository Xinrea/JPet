#include "UserStateWatcher.h"
#include "LAppPal.hpp"
#include <iostream>
#include <httplib.h>
#include <rapidjson/document.h>
#include "LAppDefine.hpp"
using namespace LAppDefine;


// https://api.vc.bilibili.com/dynamic_svr/v1/dynamic_svr/space_history?host_uid=61639371&need_top=0


void UserStateWatcher::Watch()
{
    LAppPal::PrintLog("Watch Begin");
    httplib::SSLClient liveCli("joi-club.cn", 443);
    liveCli.set_ca_cert_path("Resources/ca.crt");
    liveCli.enable_server_certificate_verification(true);
    liveCli.set_timeout_sec(1);

    httplib::SSLClient dynamicCli("api.vc.bilibili.com", 443);
    dynamicCli.set_ca_cert_path("Resources/ca.crt");
    dynamicCli.enable_server_certificate_verification(true);
    dynamicCli.set_timeout_sec(1);

    static int lastDynamic = 0;
    static INT64 lastFollow = 0;

	while(true)
	{
        // 检查直播间状态和关注数
        rapidjson::Document d;
        auto res = liveCli.Get("/api/update");
        if (res && res->status == 200) {
            if (DebugLogEnable) LAppPal::PrintLog("[UserStateWatcher]%s", res->body.c_str());
            d.Parse(res->body.c_str());
            rapidjson::Value& s = d["live"];
            isLive = s.GetInt() == 1 ? true : false;
            s = d["subscribe"];
            if (lastFollow) {
                isNewFollow = s.GetInt() > lastFollow ? true : false;
            }
            lastFollow = s.GetInt();
        }

        // 动态状态检查
        auto dres = dynamicCli.Get("/dynamic_svr/v1/dynamic_svr/space_history?host_uid=61639371&need_top=0");
        if (dres && dres->status == 200) {
            if (DebugLogEnable) LAppPal::PrintLog("[UserStateWatcher]Get Last Dynamic");
            d.Parse(dres->body.c_str());
            rapidjson::Value& s = d["data"]["cards"][0]["desc"]["timestamp"];
            if (lastDynamic) {
                isNewDynamic = s.GetInt64() > lastDynamic ? true : false;
            }
            lastDynamic = s.GetInt64();
        }

        if (isExit) break;
        std::this_thread::sleep_for(std::chrono::seconds(5));
	}
    LAppPal::PrintLog("Watch Stop");
}

std::thread UserStateWatcher::WatchThread()
{
    return std::thread(&UserStateWatcher::Watch, this);
}
