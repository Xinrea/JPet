#include "UserStateWatcher.h"
#include <iostream>
#include <httplib.h>
#include <rapidjson/document.h>
#include "LAppDefine.hpp"
using namespace LAppDefine;


// https://api.vc.bilibili.com/dynamic_svr/v1/dynamic_svr/space_history?host_uid=61639371&need_top=0

std::wstring UserStateWatcher::StringToWString(const std::string& str)
{
    int num = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    wchar_t* wide = new wchar_t[num];
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wide, num);
    std::wstring w_str(wide);
    delete[] wide;
    return w_str;
}

bool UserStateWatcher::CheckUpdate() {
    httplib::SSLClient liveCli("pet.joi-club.cn", 443);
    liveCli.set_ca_cert_path("Resources/ca.crt");
    liveCli.enable_server_certificate_verification(true);
    liveCli.set_timeout_sec(1);
    auto res = liveCli.Get("/version.txt");
    if (res && res->status == 200) {
        if (DebugLogEnable) LAppPal::PrintLog((std::string("[UserStateWatcher]Check Update Latest: ") + res->body).c_str());
        int now = _wtoi(WVERSION);
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
    httplib::SSLClient cookieCli("data.bilibili.com", 443);
    cookieCli.set_ca_cert_path("Resources/ca.crt");
    cookieCli.enable_server_certificate_verification(true);
    cookieCli.set_timeout_sec(1);

    httplib::SSLClient liveCli("api.live.bilibili.com", 443);
    liveCli.set_ca_cert_path("Resources/ca.crt");
    liveCli.enable_server_certificate_verification(true);
    liveCli.set_timeout_sec(1);

    httplib::SSLClient dynamicCli("api.vc.bilibili.com", 443);
    dynamicCli.set_ca_cert_path("Resources/ca.crt");
    dynamicCli.enable_server_certificate_verification(true);
    dynamicCli.set_timeout_sec(1);

    httplib::SSLClient nameCli("api.bilibili.com", 443);
    nameCli.set_ca_cert_path("Resources/ca.crt");
    nameCli.enable_server_certificate_verification(true);
    nameCli.set_timeout_sec(1);

    // https://data.bilibili.com/v/web/web_page_view
    static string cookies = "";

	while(true)
	{
        if (isExit) break;
        if (cookies == "")
        {
            auto cres = cookieCli.Get("/v/web/web_page_view");
            if (cres && cres->status == 200) {
                cookies = cres->get_header_value("set-cookie");
            }
        }
        httplib::Headers headers = {
            {"cookie",cookies}
        };
        for (int i = 0; i < uidlist.size(); i++)
        {
            rapidjson::Document d;
            string uid = uidlist[i];
            string roomid = "";
            wstring roomtitle = L"";
            wstring name = L"";
            // 根据UID获取昵称
            // https://api.bilibili.com/x/space/acc/info?mid=475210
            auto res = nameCli.Get(("/x/space/acc/info?mid=" + uid).c_str(), headers);
            if (res && res->status == 200) {
                d.Parse(res->body.c_str());
                if (d["code"].GetInt() == 0 && d.HasMember("data")) {
                    rapidjson::Value& s = d["data"]["name"];
                    name = StringToWString(s.GetString());
                }
            }
            else {
                if (DebugLogEnable) LAppPal::PrintLog("[UserStateWatcher]BasicInfo Failed");
                LAppPal::PrintLog(res->body.c_str());
                continue;
            }
            // 根据UID获取直播间号和直播间状态
            // https://api.live.bilibili.com/room/v1/Room/getRoomInfoOld?mid=475210
            auto infores = liveCli.Get(("/room/v1/Room/getRoomInfoOld?mid=" + uid).c_str(),headers);
            if (infores && infores->status == 200) {
                d.Parse(infores->body.c_str());
                if (d["code"].GetInt() == 0 && d.HasMember("data")) {
                    rapidjson::Value& s = d["data"]["roomid"];
                    rapidjson::Value& t = d["data"]["title"];
                    roomid = std::to_string(s.GetInt());
                    roomtitle = StringToWString(t.GetString());

                    rapidjson::Value& l = d["data"]["liveStatus"];
                    bool nowLive = l.GetInt() == 1 ? true : false;
                    // 开播了
                    if (nowLive && !lastLive[i])
                    {
                        newLive.push(StateMessage(uid, roomid, name, roomtitle, ""));
                    }
                    lastLive[i] = nowLive;
                }
            }
            else {
                if (DebugLogEnable) LAppPal::PrintLog("[UserStateWatcher]BasicInfo Failed");
                LAppPal::PrintLog(infores->body.c_str());
                continue;
            }

            // 动态状态检查
            auto dres = dynamicCli.Get(("/dynamic_svr/v1/dynamic_svr/space_history?host_uid="+uid+"&need_top=0").c_str());
            if (dres && dres->status == 200) {
                d.Parse(dres->body.c_str());
                if (d["code"].GetInt() == 0 && d.HasMember("data") && d["data"].HasMember("cards"))
                {
                    rapidjson::Value& s = d["data"]["cards"][0]["desc"]["timestamp"];
                    rapidjson::Value& id = d["data"]["cards"][0]["desc"]["dynamic_id_str"];
                    if (lastDynamic[i] != 0) {
                        if (s.GetInt64() > lastDynamic[i])
                        {
                            newDynamic.push(StateMessage(uid, roomid, name, roomtitle, id.GetString()));
                        }
                    }
                    lastDynamic[i] = s.GetInt64();
                }
            }
            else {
                if (DebugLogEnable) {
                    LAppPal::PrintLog("[UserStateWatcher]Require Dynamic Failed");
                    LAppPal::PrintLog(dres->body.c_str());
                    continue;
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
	}
    if (DebugLogEnable) LAppPal::PrintLog("[UserStateWatcher]Watch Stop");
}

std::thread UserStateWatcher::WatchThread()
{
    if (DebugLogEnable) LAppPal::PrintLog("[UserStateWatcher]Thread Start");
    return std::thread(&UserStateWatcher::Watch, this);
}
