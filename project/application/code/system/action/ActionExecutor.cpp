#include "ActionExecutor.h"

#include <Windows.h>
#include <shellapi.h>

#include <thread>

namespace {

std::wstring ToWide(const std::string& text) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return L"";

    std::wstring wide(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), wlen);
    if (!wide.empty() && wide.back() == L'\0') wide.pop_back();
    return wide;
}

std::wstring FindBrowserExe(const std::string& processName) {
    if (processName.empty()) return L"";

    std::wstring wname = ToWide(processName);
    if (wname.empty()) return L"";

    std::wstring regKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\";
    regKey += wname;

    HKEY hKey = nullptr;
    HKEY roots[] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
    for (HKEY root : roots) {
        if (RegOpenKeyExW(root, regKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t path[MAX_PATH] = {};
            DWORD size = sizeof(path);
            DWORD type = 0;
            if (RegQueryValueExW(hKey, nullptr, nullptr, &type,
                                 reinterpret_cast<LPBYTE>(path), &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                std::wstring result = path;
                if (!result.empty() && result.front() == L'"') result.erase(0, 1);
                if (!result.empty() && result.back() == L'"') result.pop_back();
                return result;
            }
            RegCloseKey(hKey);
        }
    }
    return L"";
}

} // namespace

void ActionExecutor::OpenUrl(const std::string& url, const std::string& browserProcess) {
    std::wstring wurl = ToWide(url);
    if (wurl.empty()) return;

    std::wstring exePath = FindBrowserExe(browserProcess);
    if (!exePath.empty()) {
        ShellExecuteW(nullptr, L"open", exePath.c_str(), wurl.c_str(), nullptr, SW_SHOWNORMAL);
    } else {
        ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

bool ActionExecutor::LaunchApplication(const std::string& appName) {
    std::wstring target = FindBrowserExe(appName);
    if (target.empty()) {
        target = ToWide(appName);
    }
    if (target.empty()) return false;

    HINSTANCE result = ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
}

void ActionExecutor::AutoClickFirstResult(int delayMs) {
    std::thread([delayMs]() {
        Sleep(delayMs);

        HWND hwnd = GetForegroundWindow();
        if (!hwnd) return;

        RECT rect;
        if (!GetWindowRect(hwnd, &rect)) return;

        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        int clickX = rect.left + w * 35 / 100;
        int clickY = rect.top + h * 45 / 100;

        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);

        INPUT inputs[3] = {};
        inputs[0].type = INPUT_MOUSE;
        inputs[0].mi.dx = clickX * 65535 / screenW;
        inputs[0].mi.dy = clickY * 65535 / screenH;
        inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;

        inputs[1].type = INPUT_MOUSE;
        inputs[1].mi.dx = inputs[0].mi.dx;
        inputs[1].mi.dy = inputs[0].mi.dy;
        inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE;

        inputs[2].type = INPUT_MOUSE;
        inputs[2].mi.dx = inputs[0].mi.dx;
        inputs[2].mi.dy = inputs[0].mi.dy;
        inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE;

        SendInput(3, inputs, sizeof(INPUT));
    }).detach();
}

ActionResult ActionExecutor::ExecutePlan(const ActionPlan& plan) {
    ActionResult last;
    for (const auto& step : plan.steps) {
        if (!step.tool) continue;
        ActionResult r = Execute(step.request, *step.tool);
        if (r.handled) last = r;
    }
    return last;
}

ActionResult ActionExecutor::Execute(const ActionRequest& request, const Tool& tool) {
    ActionResult result;
    result.serviceName = tool.name;
    result.query = request.query;

    if (tool.IsApp()) {
        // アプリ起動（verb は open/launch どちらでも起動）
        result.handled = LaunchApplication(tool.exe.empty() ? tool.name : tool.exe);
        return result;
    }

    // web ツール
    std::string url;
    if (request.verb == "search" && !tool.searchUrlTemplate.empty() && !request.query.empty()) {
        url = ToolRegistry::BuildSearchUrl(tool, request.query);
    } else if (!request.query.empty() && !tool.searchUrlTemplate.empty()) {
        // open でもクエリがあれば検索として扱う（「youtubeで猫」等）
        url = ToolRegistry::BuildSearchUrl(tool, request.query);
    } else {
        url = tool.openUrl;
    }
    if (url.empty()) return result;

    OpenUrl(url);
    if (request.verb == "search") AutoClickFirstResult(4000);
    result.handled = true;
    result.url = url;
    return result;
}

