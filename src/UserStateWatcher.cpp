#include "UserStateWatcher.h"
#include "LAppPal.hpp"
#include <iostream>
#include <httplib.h>
#include <rapidjson/document.h>

void UserStateWatcher::Watch()
{
    LAppPal::PrintLog("Watch Begin");
	while(true)
	{
        httplib::SSLClient cli("joi-club.cn", 443);
        cli.set_ca_cert_path("Resources/ca.crt");
        cli.enable_server_certificate_verification(true);
        auto res = cli.Get("/api/update");
        if (res && res->status == 200) {
            LAppPal::PrintLog(res->body.c_str());
            rapidjson::Document d;
            d.Parse(res->body.c_str());
            rapidjson::Value& s = d["live"];
            isLive = s.GetInt() == 1 ? true : false;
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
