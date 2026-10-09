#include "store.hpp"

#include "http.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <stdexcept>

namespace store {
namespace {

constexpr int kSearchPage = 50;
constexpr size_t kItemsBatch = 50;
constexpr int kTypeDlc = 4; // EStoreAppType

// Texto visível de um trecho HTML: remove tags e colapsa espaços.
std::string strip_html(const std::string& html) {
    std::string text = std::regex_replace(html, std::regex("<[^>]*>"), " ");
    text = std::regex_replace(text, std::regex("\\s+"), " ");
    auto first = text.find_first_not_of(' ');
    auto last = text.find_last_not_of(' ');
    return first == std::string::npos ? "" : text.substr(first, last - first + 1);
}

} // namespace

Owned user_data(auth::WebCookies& cookies) {
    http::Request req{
        .url = "https://store.steampowered.com/dynamicstore/userdata/",
        .headers = {"Accept: application/json"},
        .cookies = cookies.header(),
    };
    auto res = http::get(req);
    if (res.status != 200) throw std::runtime_error("userdata: HTTP " + std::to_string(res.status));

    auto j = nlohmann::json::parse(res.body);
    Owned owned;
    for (auto& id : j.value("rgOwnedApps", nlohmann::json::array())) owned.apps.insert(id.get<uint32_t>());
    for (auto& id : j.value("rgOwnedPackages", nlohmann::json::array())) owned.packages.insert(id.get<uint32_t>());
    for (const auto& [name, value] : res.cookies)
        if (name != "steamLoginSecure" && name != "sessionid") cookies.extra.insert_or_assign(name, value);
    if (auto it = res.cookies.find("steamCountry"); it != res.cookies.end())
        owned.country = it->second.substr(0, it->second.find('%')); // "BR%7C<hash>"
    return owned;
}

std::vector<uint32_t> search_free_promos(bool games_only) {
    static const std::regex appid_re(R"re(data-ds-appid="(\d+)")re");
    std::vector<uint32_t> appids;
    for (int start = 0;; start += kSearchPage) {
        std::string url = "https://store.steampowered.com/search/results/"
                          "?maxprice=free&specials=1&hwtype=0&ndl=1&infinite=1"
                          "&start=" + std::to_string(start) + "&count=" + std::to_string(kSearchPage);
        if (games_only) url += "&category1=998";

        auto res = http::get({.url = url, .headers = {"Accept: application/json"}, .cookies = {}});
        if (res.status != 200) throw std::runtime_error("busca: HTTP " + std::to_string(res.status));
        auto j = nlohmann::json::parse(res.body);
        const std::string html = j.value("results_html", "");

        size_t before = appids.size();
        for (std::sregex_iterator it(html.begin(), html.end(), appid_re), end; it != end; ++it)
            appids.push_back(static_cast<uint32_t>(std::stoul((*it)[1])));

        // Bundles/pacotes na busca não têm data-ds-appid; página sem nada novo ⇒ fim.
        if (appids.size() == before || start + kSearchPage >= j.value("total_count", 0)) break;
    }
    return appids;
}

std::vector<Promo> free_to_keep(const std::vector<uint32_t>& appids, const std::string& country) {
    std::vector<Promo> promos;
    for (size_t i = 0; i < appids.size(); i += kItemsBatch) {
        nlohmann::json ids = nlohmann::json::array();
        for (size_t k = i; k < std::min(i + kItemsBatch, appids.size()); ++k) ids.push_back({{"appid", appids[k]}});
        nlohmann::json input = {
            {"ids", ids},
            {"context", {{"language", "brazilian"}, {"country_code", country}}},
            {"data_request", {{"include_all_purchase_options", true}}},
        };

        auto res = http::get({
            .url = "https://api.steampowered.com/IStoreBrowseService/GetItems/v1/?input_json=" +
                   http::url_encode(input.dump()),
            .headers = {"Accept: application/json"},
            .cookies = {},
        });
        if (res.status != 200) throw std::runtime_error("GetItems: HTTP " + std::to_string(res.status));

        auto items = nlohmann::json::parse(res.body)["response"].value("store_items", nlohmann::json::array());
        for (const auto& item : items) {
            for (const auto& po : item.value("purchase_options", nlohmann::json::array())) {
                // Só pacotes (não bundles) "grátis para manter" a preço zero.
                if (!po.value("is_free_to_keep", false) || !po.contains("packageid")) continue;
                if (po.value("final_price_in_cents", "1") != "0") continue;
                Promo p;
                p.appid = item.value("appid", 0u);
                p.subid = po["packageid"].get<uint32_t>();
                p.name = item.value("name", "");
                p.is_dlc = item.value("type", 0) == kTypeDlc;
                if (item.contains("related_items")) p.parent_appid = item["related_items"].value("parent_appid", 0u);
                p.ends = po.value("free_to_keep_ends", std::time_t{0});
                promos.push_back(std::move(p));
            }
        }
    }
    return promos;
}

ClaimResult claim(const auth::WebCookies& cookies, uint32_t subid) {
    http::Request req{
        .url = "https://store.steampowered.com/freelicense/addfreelicense/",
        .headers = {"Origin: https://store.steampowered.com",
                    "Referer: https://store.steampowered.com/"},
        .cookies = cookies.header(),
    };
    auto res = http::post_form(req, {
                                        {"action", "add_to_cart"},
                                        {"sessionid", cookies.sessionid},
                                        {"subid", std::to_string(subid)},
                                    });

    if (std::getenv("GAMECATCHER_DEBUG")) std::ofstream("claim_" + std::to_string(subid) + ".html") << res.body;

    ClaimResult out;
    out.ok = res.status == 200 && res.body.find("add_free_content_success_area") != std::string::npos;

    // Mensagem da página: <h3> no sucesso; nas falhas, o #error_box ("Erro do site").
    auto text_after = [&](const std::string& marker, const std::string& until) -> std::string {
        auto at = res.body.find(marker);
        if (at == std::string::npos) return {};
        at = res.body.find('>', at);
        if (at == std::string::npos) return {};
        auto end = res.body.find(until, at);
        if (end == std::string::npos || end - at > 2000) end = at + 2000;
        return strip_html(res.body.substr(at + 1, end - at - 1));
    };
    out.message = out.ok ? text_after("add_free_content_success_area", "</h3>")
                         : text_after("id=\"error_box\"", "</div>");
    if (out.message.empty()) out.message = "HTTP " + std::to_string(res.status);
    return out;
}

} // namespace store
