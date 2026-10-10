// Toast nativo do Windows 10/11 via C++/WinRT. NÃO TESTADO (escrito sem acesso a Windows).

#include "notify.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shobjidl_core.h>

#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace notify {
namespace {

using namespace winrt::Windows::UI::Notifications;
using winrt::Windows::Data::Xml::Dom::XmlDocument;

constexpr const wchar_t* kAumid = L"gamecatcher.notifier";

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring xml_escape(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        switch (c) {
        case L'&': out += L"&amp;"; break;
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        case L'"': out += L"&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

// App sem instalador/MSIX: o AUMID precisa estar registrado em HKCU para o toast aparecer
// com nome próprio (mesma técnica do DesktopNotificationManagerCompat do Windows Community Toolkit).
void register_aumid() {
    HKEY key;
    std::wstring path = std::wstring(L"Software\\Classes\\AppUserModelId\\") + kAumid;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) ==
        ERROR_SUCCESS) {
        const wchar_t name[] = L"gamecatcher";
        RegSetValueExW(key, L"DisplayName", 0, REG_SZ, reinterpret_cast<const BYTE*>(name), sizeof name);
        RegCloseKey(key);
    }
    SetCurrentProcessExplicitAppUserModelID(kAumid);
}

// Toast com botões; `actions`: pares (argumento devolvido no clique, texto do botão).
ToastNotification make_toast(const std::string& title, const std::string& body,
                             const std::vector<std::pair<std::string, std::string>>& actions) {
    std::wstring xml =
        L"<toast scenario=\"reminder\">"
        L"<visual><binding template=\"ToastGeneric\">"
        L"<text>" + xml_escape(widen(title)) + L"</text>"
        L"<text>" + xml_escape(widen(body)) + L"</text>"
        L"</binding></visual>"
        L"<actions>";
    for (const auto& [arg, text] : actions)
        xml += L"<action content=\"" + xml_escape(widen(text)) + L"\" arguments=\"" + xml_escape(widen(arg)) +
               L"\" activationType=\"foreground\"/>";
    xml += L"</actions></toast>";
    XmlDocument doc;
    doc.LoadXml(xml);
    return ToastNotification(doc);
}

// Liga os eventos do toast a `answer`, chamada uma única vez com o argumento do botão clicado
// ("" = fechado sem escolher). Activated e Dismissed podem chegar os dois.
void on_answer(const ToastNotification& toast, std::function<void(const std::wstring&)> answer) {
    auto once = std::make_shared<std::once_flag>();
    auto call = [once, answer = std::move(answer)](const std::wstring& a) { std::call_once(*once, answer, a); };
    toast.Activated([call](const ToastNotification&, const winrt::Windows::Foundation::IInspectable& args) {
        call(std::wstring(args.as<ToastActivatedEventArgs>().Arguments()));
    });
    toast.Dismissed([call](const ToastNotification&, const ToastDismissedEventArgs& args) {
        // TimedOut = foi para a Central de Ações; os botões continuam lá, então seguimos esperando.
        if (args.Reason() != ToastDismissalReason::TimedOut) call(L"");
    });
    toast.Failed([call](const ToastNotification&, const ToastFailedEventArgs&) { call(L""); });
}

} // namespace

void ask(const std::vector<store::Promo>& promos, const std::string& account,
         const std::function<void(const store::Promo&, Choice)>& on_choice) {
    winrt::init_apartment();
    register_aumid();
    auto notifier = ToastNotificationManager::CreateToastNotifier(kAumid);

    std::mutex mu;
    std::condition_variable cv;
    size_t pending = promos.size();
    std::vector<ToastNotification> toasts; // manter vivos enquanto esperamos os eventos

    for (const auto& p : promos) {
        auto toast = make_toast(detail::title(p), detail::body(p, account), {{"claim", "Resgatar"}, {"ignore", "Ignorar"}});
        on_answer(toast, [&, &p = p](const std::wstring& a) {
            std::lock_guard lock(mu);
            on_choice(p, a == L"claim" ? Choice::Claim : a == L"ignore" ? Choice::Ignore : Choice::Dismissed);
            --pending;
            cv.notify_all();
        });
        notifier.Show(toast);
        toasts.push_back(toast);
    }

    std::unique_lock lock(mu);
    cv.wait(lock, [&] { return pending == 0; });
}

bool confirm(const std::string& title, const std::string& body, const std::string& yes, const std::string& no) {
    winrt::init_apartment(); // chamada de outras threads (fila de resgates, atualização)
    register_aumid();
    auto notifier = ToastNotificationManager::CreateToastNotifier(kAumid);

    // Compartilhado com os eventos: um evento atrasado não pode acessar esta pilha depois do return.
    struct State {
        std::mutex mu;
        std::condition_variable cv;
        std::optional<bool> yes;
    };
    auto st = std::make_shared<State>();

    auto toast = make_toast(title, body, {{"yes", yes}, {"no", no}});
    on_answer(toast, [st](const std::wstring& a) {
        std::lock_guard lock(st->mu);
        st->yes = a == L"yes";
        st->cv.notify_all();
    });
    notifier.Show(toast);

    std::unique_lock lock(st->mu);
    st->cv.wait(lock, [&] { return st->yes.has_value(); });
    return *st->yes;
}

namespace detail {

// A Steam registra o pid em HKCU\Software\Valve\Steam\ActiveProcess (0 quando fechada).
std::optional<std::chrono::seconds> steam_age() {
    DWORD pid = 0, size = sizeof pid;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"pid", RRF_RT_REG_DWORD, nullptr,
                     &pid, &size) != ERROR_SUCCESS ||
        pid == 0)
        return std::nullopt;

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return std::nullopt; // o pid ficou no registro, mas o processo já fechou

    // O pid pode ter sido reaproveitado por outro programa: confere que é a steam.exe e que está viva.
    wchar_t image[MAX_PATH];
    DWORD len = MAX_PATH, exit_code = 0;
    FILETIME created{}, exited{}, kernel{}, user{};
    bool ok = QueryFullProcessImageNameW(h, 0, image, &len) && GetExitCodeProcess(h, &exit_code) &&
              exit_code == STILL_ACTIVE && GetProcessTimes(h, &created, &exited, &kernel, &user);
    CloseHandle(h);
    std::wstring name(image, ok ? len : 0);
    auto slash = name.find_last_of(L"\\/");
    if (!ok || _wcsicmp(name.substr(slash == std::wstring::npos ? 0 : slash + 1).c_str(), L"steam.exe") != 0)
        return std::nullopt;

    FILETIME now_ft;
    GetSystemTimeAsFileTime(&now_ft);
    auto ticks = [](FILETIME f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    uint64_t now = ticks(now_ft), start = ticks(created); // unidades de 100 ns
    return std::chrono::seconds(now > start ? (now - start) / 10'000'000 : 0);
}

// ShellExecute retorna <= 32 em erro (ex.: nenhum programa para steam://).
bool open_url(const std::string& url) {
    std::wstring w = widen(url);
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

} // namespace detail

} // namespace notify
