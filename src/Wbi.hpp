#pragma once
#include <array>   // std::array
#include <memory>
#include <regex>

/// thrid party libraries
#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include <cpr/cpr.h>
#include <cryptopp/md5.h>
#include <cryptopp/hex.h>
#include <nlohmann/json.hpp>

struct WbiConfig {
    std::string img_key;
    std::string sub_key;
};

class Wbi {
  constexpr static std::array<uint8_t, 64> MIXIN_KEY_ENC_TAB_ = {
      46, 47, 18, 2,  53, 8,  23, 32, 15, 50, 10, 31, 58, 3,  45, 35,
      27, 43, 5,  49, 33, 9,  42, 19, 29, 28, 14, 39, 12, 38, 41, 13,
      37, 48, 7,  16, 24, 55, 40, 61, 26, 17, 0,  1,  60, 51, 30, 4,
      22, 25, 54, 21, 56, 59, 6,  63, 57, 62, 11, 36, 20, 34, 44, 52};

  /* 获取 md5 hex(lower) */
  static std::string Get_md5_hex(const std::string &Input_str) {
    CryptoPP::Weak1::MD5 hash;
    std::string md5_hex;

    CryptoPP::StringSource ss(
        Input_str, true,
        new CryptoPP::HashFilter(
            hash, new CryptoPP::HexEncoder(new CryptoPP::StringSink(md5_hex))));

    // lower case md5_hex
    std::transform(md5_hex.begin(), md5_hex.end(), md5_hex.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return md5_hex;
  }

 public:
  /* 将 json 转换为 url 编码字符串 */
  static std::string Json_to_url_encode_str(const nlohmann::json &Json) {
    std::string encode_str;
    for (const auto &[key, value] : Json.items()) {
      encode_str.append(key)
          .append("=")
          .append(cpr::util::urlEncode(
              value.is_string() ? value.get<std::string>() : to_string(value)))
          .append("&");
    }

    // remove the last '&'
    encode_str.resize(encode_str.size() - 1, '\0');
    return encode_str;
  }

  /* 获取 wbi key */
  static std::shared_ptr<WbiConfig> Get_wbi_key() {
    const auto url = cpr::Url{"https://api.bilibili.com/x/web-interface/nav"};
    const auto cookie = cpr::Cookies{
        {"SESSDATA", "xxxxxxxxxxxx"},
    };
    const auto header = cpr::Header{
        {"User-Agent",
         "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
         "like Gecko) Chrome/58.0.3029.110 Safari/537.3"},
        {"Referer", "https://www.bilibili.com/"},
    };
    const auto response = cpr::Get(url, cookie, header);

    if (response.status_code != 200) {
        return nullptr;
    }

    nlohmann::json json = nlohmann::json::parse(response.text);

    const std::string img_url = json["data"]["wbi_img"]["img_url"];
    const std::string sub_url = json["data"]["wbi_img"]["sub_url"];

    std::string img_key =
        img_url.substr(img_url.find("wbi/") + 4,
                       img_url.find(".png") - img_url.find("wbi/") - 4);
    std::string sub_key =
        sub_url.substr(sub_url.find("wbi/") + 4,
                       sub_url.find(".png") - sub_url.find("wbi/") - 4);
    auto ret = std::make_shared<WbiConfig>();
    ret->img_key = img_key;
    ret->sub_key = sub_key;
    return ret;
  }

  static std::optional<std::string> GetWebId() {
    const auto url = cpr::Url{"https://space.bilibili.com/475210"};
    const auto cookie = cpr::Cookies{
        {"SESSDATA", "xxxxxxxxxxxx"},
    };
    const auto header = cpr::Header{
        {"User-Agent",
         "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
         "like Gecko) Chrome/58.0.3029.110 Safari/537.3"},
        {"Referer", "https://www.bilibili.com/"},
    };
    const auto response = cpr::Get(url, cookie, header);
    if (response.status_code != 200) {
        return std::nullopt;
    }
    // let re = Regex::new(r#"<script id="__RENDER_DATA__" type="application/json">(.+?)</script>"#).unwrap();
    const std::string re = R"(<script id="__RENDER_DATA__" type="application/json">(.+?)</script>)";
    std::regex reg(re);
    std::smatch match;
    if (std::regex_search(response.text, match, reg)) {
        const std::string str = match[1];
        // urldecode str
        std::string decoded_str = cpr::util::urlDecode(str);
        nlohmann::json json = nlohmann::json::parse(decoded_str);
        return json["access_id"].get<std::string>();
    }
    return std::nullopt;
  }

  /* 获取 mixin key */
  static std::string Get_mixin_key(const std::string &Img_key,
                                   const std::string &Sub_key) {
    std::string raw_wbi_key_str = Img_key + Sub_key;
    std::string result;

    // std::ranges::for_each(MIXIN_KEY_ENC_TAB_,
    //                       [&result, &raw_wbi_key_str](const uint8_t x) {
    //                         result.push_back(raw_wbi_key_str.at(x));
    //                       });
    std::transform(
        MIXIN_KEY_ENC_TAB_.begin(), MIXIN_KEY_ENC_TAB_.end(),
        std::back_inserter(result),
        [&raw_wbi_key_str](const uint8_t x) { return raw_wbi_key_str.at(x); });

    return result.substr(0, 32);
  }

  /* 计算签名(w_rid) */
  static std::string Calc_sign(nlohmann::json &Params,
                               const std::string &Mixin_key) {
    static std::optional<std::string> w_webid = GetWebId();
    static int w_webid_ts = 0;
    // may expire in 20 hours
    if (!w_webid.has_value() || w_webid_ts == 0 || w_webid_ts + 72000 < std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count()) {
      w_webid = GetWebId();
      w_webid_ts = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
    }
    Params["w_webid"] = w_webid.value_or("");
    Params["wts"] = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();

    const std::string encode_str =
        Json_to_url_encode_str(Params).append(Mixin_key);
    return Get_md5_hex(encode_str);
  }
};
