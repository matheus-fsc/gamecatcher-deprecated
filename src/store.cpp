#include "store.hpp"

#include "http.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <regex>
#include <stdexcept>

namespace store {
namespace {

constexpr int kSearchPage = 50;
constexpr size_t kItemsBatch = 50;
constexpr int kTypeDlc = 4; // EStoreAppType

nlohmann::json get_items(const std::vector<uint32_t>& appids, const std::string& country) {
    nlohmann::json all = nlohmann::json::array();
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
        for (auto& item : nlohmann::json::parse(res.body)["response"].value("store_items", nlohmann::json::array()))
            all.push_back(std::move(item));
    }
    return all;
}

} // namespace

SearchResult search_free_promos(bool games_only) {
    static const std::regex appid_re(R"re(data-ds-appid="(\d+)")re");
    SearchResult out;
    for (int start = 0;; start += kSearchPage) {
        std::string url = "https://store.steampowered.com/search/results/"
                          "?maxprice=free&specials=1&hwtype=0&ndl=1&infinite=1"
                          "&start=" + std::to_string(start) + "&count=" + std::to_string(kSearchPage);
        if (games_only) url += "&category1=998";

        auto res = http::get({.url = url, .headers = {"Accept: application/json"}, .cookies = {}});
        if (res.status != 200) throw std::runtime_error("busca: HTTP " + std::to_string(res.status));
        if (auto it = res.cookies.find("steamCountry"); it != res.cookies.end() && out.country.empty())
            out.country = it->second.substr(0, it->second.find('%')); // "BR%7C<hash>"

        auto j = nlohmann::json::parse(res.body);
        const std::string html = j.value("results_html", "");
        size_t before = out.appids.size();
        for (std::sregex_iterator it(html.begin(), html.end(), appid_re), end; it != end; ++it)
            out.appids.push_back(static_cast<uint32_t>(std::stoul((*it)[1])));

        // Bundles/pacotes na busca não têm data-ds-appid; página sem nada novo: fim.
        if (out.appids.size() == before || start + kSearchPage >= j.value("total_count", 0)) break;
    }
    return out;
}

std::vector<Promo> free_to_keep(const std::vector<uint32_t>& appids, const std::string& country) {
    std::vector<Promo> promos;
    std::map<uint32_t, std::vector<size_t>> by_parent; // jogo base -> índices das DLCs

    for (const auto& item : get_items(appids, country)) {
        for (const auto& po : item.value("purchase_options", nlohmann::json::array())) {
            // Só pacotes (não bundles) "grátis para manter" a preço zero.
            if (!po.value("is_free_to_keep", false) || !po.contains("packageid")) continue;
            if (po.value("final_price_in_cents", "1") != "0") continue;
            Promo p;
            p.appid = item.value("appid", 0u);
            p.subid = po["packageid"].get<uint32_t>();
            p.name = item.value("name", "");
            p.is_dlc = item.value("type", 0) == kTypeDlc;
            p.original_price = po.value("formatted_original_price", "");
            p.ends = po.value("free_to_keep_ends", std::time_t{0});
            if (p.is_dlc && item.contains("related_items"))
                if (uint32_t parent = item["related_items"].value("parent_appid", 0u)) {
                    p.parent_appid = parent;
                    by_parent[parent].push_back(promos.size());
                }
            promos.push_back(std::move(p));
        }
    }

    // Nome dos jogos base, para a notificação dizer "DLC de X".
    if (!by_parent.empty()) {
        std::vector<uint32_t> parents;
        for (const auto& [id, _] : by_parent) parents.push_back(id);
        for (const auto& item : get_items(parents, country))
            for (size_t idx : by_parent[item.value("appid", 0u)]) promos[idx].parent_name = item.value("name", "");
    }
    return promos;
}

} // namespace store
