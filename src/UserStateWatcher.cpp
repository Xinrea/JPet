#include "UserStateWatcher.h"
#include "LAppPal.hpp"
#include <iostream>
#include <httplib.h>
#include <rapidjson/document.h>
#include <string>
#include "LAppDefine.hpp"
using namespace LAppDefine;


// https://api.vc.bilibili.com/dynamic_svr/v1/dynamic_svr/space_history?host_uid=61639371&need_top=0

bool UserStateWatcher::CheckUpdate() {
    httplib::SSLClient liveCli("pet.joi-club.cn", 443);
    liveCli.set_ca_cert_path("Resources/ca.crt");
    liveCli.enable_server_certificate_verification(true);
    liveCli.set_timeout_sec(1);
    auto res = liveCli.Get("/version.txt");
    if (res && res->status == 200) {
        if (DebugLogEnable) LAppPal::PrintLog((std::string("[UserStateWatcher]Check Update Latest: ") + res->body).c_str());
        int now = _wtoi(VERSION);
        int latest = atoi(res->body.c_str());
        if (now < latest) {
            LAppPal::PrintLog("[UserStateWatcher]Need Update: %s -> %s ",VERSION, res->body.c_str());
            return true;
        }
        else return false;
    }
    else {
        if (DebugLogEnable) LAppPal::PrintLog("[UserStateWatcher]Check Update Failed");
    }
    return false;
}


void UserStateWatcher::Watch()
{
    LAppPal::PrintLog("Watch Begin");
    httplib::SSLClient liveCli("api.live.bilibili.com", 443);
    liveCli.set_ca_cert_path("Resources/ca.crt");
    liveCli.enable_server_certificate_verification(true);
    liveCli.set_timeout_sec(1);

    httplib::SSLClient dynamicCli("api.vc.bilibili.com", 443);
    dynamicCli.set_ca_cert_path("Resources/ca.crt");
    dynamicCli.enable_server_certificate_verification(true);
    dynamicCli.set_timeout_sec(1);

    httplib::SSLClient subCli("api.bilibili.com", 443);
    subCli.set_ca_cert_path("Resources/ca.crt");
    subCli.enable_server_certificate_verification(true);
    subCli.set_timeout_sec(1);



    static int lastDynamic = 0;
    static INT64 lastFollow = 0;

	while(true)
	{
        // 检查直播间状态和关注数
        rapidjson::Document d;
        auto res = liveCli.Get("/room/v1/Room/room_init?id=21484828");
        auto sres = subCli.Get("/x/relation/stat?vmid=61639371");
        if (res && res->status == 200) {
            d.Parse(res->body.c_str());
            if (d.HasMember("data")) {
                rapidjson::Value& s = d["data"]["live_status"];
                isLive = s.GetInt() == 1 ? true : false;
            }
        }
        else {
            if (DebugLogEnable) LAppPal::PrintLog("[UserStateWatcher]live_status Failed");
        }

        if (sres && sres->status == 200) {
            d.Parse(sres->body.c_str());
            if (d.HasMember("data")) {
                rapidjson::Value& s = d["data"]["follower"];
                if (lastFollow) {
                    isNewFollow = s.GetInt() > lastFollow ? true : false;
                }
                lastFollow = s.GetInt();
            }
        }
        else {
            if (DebugLogEnable) LAppPal::PrintLog("[UserStateWatcher]/x/relation/stat Failed");
        }

        // 动态状态检查
        auto dres = dynamicCli.Get("/dynamic_svr/v1/dynamic_svr/space_history?host_uid=61639371&need_top=0");
        if (dres && dres->status == 200) {
            d.Parse(dres->body.c_str());
            if (d.HasMember("data"))
            {
                rapidjson::Value& s = d["data"]["cards"][0]["desc"]["timestamp"];
                if (lastDynamic) {
                    isNewDynamic = s.GetInt64() > lastDynamic ? true : false;
                }
                lastDynamic = s.GetInt64();
            }
        }
        else {
            if (DebugLogEnable) LAppPal::PrintLog("[UserStateWatcher]Require Dynamic Failed");
        }

        if (isExit) break;
        std::this_thread::sleep_for(std::chrono::seconds(5));
	}
    if (DebugLogEnable) LAppPal::PrintLog("[UserStateWatcher]Watch Stop");
}

std::thread UserStateWatcher::WatchThread()
{
    if (DebugLogEnable) LAppPal::PrintLog("[UserStateWatcher]Thread Start");
    return std::thread(&UserStateWatcher::Watch, this);
}
