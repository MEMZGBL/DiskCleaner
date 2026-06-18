#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

constexpr int IDC_SCAN = 1001;
constexpr int IDC_CLEAN = 1002;
constexpr int IDC_LOG = 1003;
constexpr int IDC_STATUS = 1004;
constexpr int IDC_LIST = 1005;
constexpr int IDC_PROGRESS = 1006;
constexpr int IDC_SELECT_SAFE = 1007;
constexpr int IDC_TITLE = 1008;
constexpr int IDC_SUBTITLE = 1009;
constexpr int IDC_ABOUT = 1010;
constexpr int IDC_ABOUT_GITHUB = 1011;
constexpr int IDC_ABOUT_BILIBILI = 1012;

constexpr UINT WM_APP_LOG = WM_APP + 1;
constexpr UINT WM_APP_STATUS = WM_APP + 2;
constexpr UINT WM_APP_PROGRESS = WM_APP + 3;
constexpr UINT WM_APP_SCAN_DONE = WM_APP + 4;
constexpr UINT WM_APP_CLEAN_DONE = WM_APP + 5;

struct Category {
    std::wstring name;
    std::vector<fs::path> roots;
    bool selected = true;
    bool safeDefault = true;
    int minAgeHours = 1;
    bool recycleBin = false;
    uintmax_t bytes = 0;
    uintmax_t files = 0;
};

struct AppState {
    HWND hwnd = nullptr;
    HWND list = nullptr;
    HWND log = nullptr;
    HWND status = nullptr;
    HWND progress = nullptr;
    HWND scanButton = nullptr;
    HWND cleanButton = nullptr;
    HWND selectSafeButton = nullptr;
    HWND aboutButton = nullptr;
    HWND title = nullptr;
    HWND subtitle = nullptr;
    HFONT titleFont = nullptr;
    HFONT bodyFont = nullptr;
    HBRUSH bgBrush = nullptr;
    HBRUSH panelBrush = nullptr;
    std::vector<Category> categories;
    std::atomic_bool busy{false};
    std::mutex dataMutex;
};

AppState g_app;

COLORREF RGB_BG = RGB(243, 243, 243);
COLORREF RGB_PANEL = RGB(255, 255, 255);
COLORREF RGB_TEXT = RGB(32, 32, 32);
COLORREF RGB_MUTED = RGB(96, 96, 96);
COLORREF RGB_ACCENT = RGB(0, 95, 184);
COLORREF RGB_ACCENT_HOVER = RGB(0, 83, 161);
COLORREF RGB_BUTTON = RGB(251, 251, 251);
COLORREF RGB_BUTTON_BORDER = RGB(210, 210, 210);
COLORREF RGB_FRAME_BORDER = RGB(218, 218, 218);

std::wstring getEnv(const wchar_t* key) {
    DWORD needed = GetEnvironmentVariableW(key, nullptr, 0);
    if (!needed) return L"";
    std::wstring value(needed, L'\0');
    GetEnvironmentVariableW(key, value.data(), needed);
    if (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

fs::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw) {
        fs::path p(raw);
        CoTaskMemFree(raw);
        return p;
    }
    return {};
}

std::wstring formatBytes(uintmax_t bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(unit == 0 ? 0 : 2) << value << L" " << units[unit];
    return ss.str();
}

std::wstring lowerPath(const fs::path& p) {
    std::error_code ec;
    std::wstring s = fs::weakly_canonical(p, ec).wstring();
    if (s.empty()) s = p.wstring();
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return s;
}

bool isProtectedPath(const fs::path& p) {
    std::wstring value = lowerPath(p);
    std::vector<std::wstring> protectedRoots = {
        lowerPath(L"C:\\"),
        lowerPath(L"C:\\Windows"),
        lowerPath(L"C:\\Windows\\System32"),
        lowerPath(L"C:\\Program Files"),
        lowerPath(L"C:\\Program Files (x86)"),
        lowerPath(getEnv(L"USERPROFILE")),
        lowerPath(knownFolder(FOLDERID_Desktop)),
        lowerPath(knownFolder(FOLDERID_Documents)),
        lowerPath(knownFolder(FOLDERID_Pictures)),
        lowerPath(knownFolder(FOLDERID_Music)),
        lowerPath(knownFolder(FOLDERID_Videos))
    };
    for (const auto& root : protectedRoots) {
        if (!root.empty() && value == root) return true;
    }
    return false;
}

bool oldEnough(const fs::path& p, int minAgeHours) {
    if (minAgeHours <= 0) return true;
    std::error_code ec;
    auto writeTime = fs::last_write_time(p, ec);
    if (ec) return false;
    auto now = fs::file_time_type::clock::now();
    return now - writeTime >= std::chrono::hours(minAgeHours);
}

void postString(UINT message, const std::wstring& text) {
    PostMessageW(g_app.hwnd, message, 0, reinterpret_cast<LPARAM>(new std::wstring(text)));
}

void appendLog(const std::wstring& text) {
    int len = GetWindowTextLengthW(g_app.log);
    SendMessageW(g_app.log, EM_SETSEL, len, len);
    std::wstring line = text + L"\r\n";
    SendMessageW(g_app.log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}

void addIf(std::vector<fs::path>& roots, const fs::path& path) {
    if (!path.empty()) roots.push_back(path);
}

void addProfileSubdirs(std::vector<fs::path>& roots, const fs::path& base, const std::vector<fs::path>& suffixes) {
    std::error_code ec;
    if (base.empty() || !fs::exists(base, ec) || !fs::is_directory(base, ec)) return;
    for (const auto& entry : fs::directory_iterator(base, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;
        for (const auto& suffix : suffixes) {
            roots.push_back(entry.path() / suffix);
        }
    }
}

void addBrowserProfileCaches(std::vector<fs::path>& roots, const fs::path& userDataRoot) {
    std::error_code ec;
    if (userDataRoot.empty() || !fs::exists(userDataRoot, ec) || !fs::is_directory(userDataRoot, ec)) return;
    for (const auto& entry : fs::directory_iterator(userDataRoot, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;
        std::wstring name = entry.path().filename().wstring();
        bool profile = name == L"Default" || name == L"Profile 1" || name == L"Profile 2" ||
            name == L"Profile 3" || name == L"Profile 4" || name == L"Profile 5";
        if (!profile) continue;
        addIf(roots, entry.path() / L"Cache");
        addIf(roots, entry.path() / L"Code Cache");
        addIf(roots, entry.path() / L"GPUCache");
        addIf(roots, entry.path() / L"Service Worker\\CacheStorage");
        addIf(roots, entry.path() / L"ShaderCache");
    }
}

void addFirefoxCaches(std::vector<fs::path>& roots, const fs::path& profilesRoot) {
    addProfileSubdirs(roots, profilesRoot, {
        L"cache2",
        L"startupCache",
        L"jumpListCache"
    });
}

void setControlsEnabled(bool enabled) {
    EnableWindow(g_app.scanButton, enabled);
    EnableWindow(g_app.cleanButton, enabled);
    EnableWindow(g_app.selectSafeButton, enabled);
    EnableWindow(g_app.aboutButton, enabled);
    EnableWindow(g_app.list, enabled);
}

HFONT makeFont(int pointSize, int weight) {
    HDC dc = GetDC(nullptr);
    int height = -MulDiv(pointSize, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, dc);
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

void applyFont(HWND hwnd, HFONT font) {
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void fillRoundRect(HDC dc, RECT rc, int radius, COLORREF color, COLORREF border) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void setRoundRegion(HWND hwnd, int width, int height, int radius) {
    if (!hwnd || width <= 0 || height <= 0) return;
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius, radius);
    SetWindowRgn(hwnd, region, TRUE);
}

void applyRoundedRegions() {
    RECT rc{};
    if (GetWindowRect(g_app.list, &rc)) {
        setRoundRegion(g_app.list, rc.right - rc.left, rc.bottom - rc.top, 10);
    }
    if (GetWindowRect(g_app.log, &rc)) {
        setRoundRegion(g_app.log, rc.right - rc.left, rc.bottom - rc.top, 10);
    }
    if (GetWindowRect(g_app.progress, &rc)) {
        setRoundRegion(g_app.progress, rc.right - rc.left, rc.bottom - rc.top, 8);
    }
}

void drawButton(const DRAWITEMSTRUCT* dis) {
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool focus = (dis->itemState & ODS_FOCUS) != 0;
    bool primary = dis->CtlID == IDC_CLEAN;

    RECT rc = dis->rcItem;
    COLORREF fill = primary ? RGB_ACCENT : RGB_BUTTON;
    COLORREF border = primary ? RGB_ACCENT : RGB_BUTTON_BORDER;
    COLORREF text = primary ? RGB(255, 255, 255) : RGB_TEXT;
    if (disabled) {
        fill = RGB(235, 235, 235);
        border = RGB(224, 224, 224);
        text = RGB(150, 150, 150);
    } else if (pressed) {
        fill = primary ? RGB_ACCENT_HOVER : RGB(238, 238, 238);
    }

    fillRoundRect(dis->hDC, rc, 8, fill, border);
    if (focus && !disabled) {
        RECT focusRc = rc;
        InflateRect(&focusRc, -3, -3);
        HPEN pen = CreatePen(PS_DOT, 1, primary ? RGB(255, 255, 255) : RGB_ACCENT);
        HGDIOBJ oldPen = SelectObject(dis->hDC, pen);
        HGDIOBJ oldBrush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        RoundRect(dis->hDC, focusRc.left, focusRc.top, focusRc.right, focusRc.bottom, 6, 6);
        SelectObject(dis->hDC, oldBrush);
        SelectObject(dis->hDC, oldPen);
        DeleteObject(pen);
    }

    wchar_t textBuffer[128]{};
    GetWindowTextW(dis->hwndItem, textBuffer, 128);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, text);
    SelectObject(dis->hDC, g_app.bodyFont);
    DrawTextW(dis->hDC, textBuffer, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void openUrl(const wchar_t* url) {
    ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CALLBACK aboutProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HWND title = CreateWindowW(L"STATIC", L"磁盘清理 By MEMZGBL", WS_CHILD | WS_VISIBLE,
            24, 22, 420, 32, hwnd, nullptr, nullptr, nullptr);
        HWND text = CreateWindowW(L"STATIC", L"本工具特地为计算机小白编写\r\n\r\n制作人员: MEMZGBL",
            WS_CHILD | WS_VISIBLE, 24, 68, 420, 64, hwnd, nullptr, nullptr, nullptr);
        HWND github = CreateWindowW(L"BUTTON", L"GitHub: https://github.com/memzgbl",
            WS_CHILD | WS_VISIBLE | BS_FLAT, 24, 146, 360, 30, hwnd,
            reinterpret_cast<HMENU>(IDC_ABOUT_GITHUB), nullptr, nullptr);
        HWND bili = CreateWindowW(L"BUTTON", L"哔哩哔哩: https://space.bilibili.com/326402501",
            WS_CHILD | WS_VISIBLE | BS_FLAT, 24, 184, 420, 30, hwnd,
            reinterpret_cast<HMENU>(IDC_ABOUT_BILIBILI), nullptr, nullptr);
        HWND close = CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            356, 232, 88, 32, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
        applyFont(title, g_app.titleFont);
        applyFont(text, g_app.bodyFont);
        applyFont(github, g_app.bodyFont);
        applyFont(bili, g_app.bodyFont);
        applyFont(close, g_app.bodyFont);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_ABOUT_GITHUB) {
            openUrl(L"https://github.com/memzgbl");
        } else if (LOWORD(wParam) == IDC_ABOUT_BILIBILI) {
            openUrl(L"https://space.bilibili.com/326402501");
        } else if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB_TEXT);
        return reinterpret_cast<LRESULT>(g_app.panelBrush);
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void showAboutDialog(HWND owner) {
    const wchar_t className[] = L"DiskCleanerAboutWindow";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = aboutProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = className;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIcon(nullptr, IDI_INFORMATION);
        wc.hbrBackground = g_app.panelBrush;
        RegisterClassW(&wc);
        registered = true;
    }
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, className, L"关于",
        WS_CAPTION | WS_SYSMENU | WS_POPUPWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 320,
        owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dialog) return;
    RECT ownerRc{}, dialogRc{};
    GetWindowRect(owner, &ownerRc);
    GetWindowRect(dialog, &dialogRc);
    int x = ownerRc.left + ((ownerRc.right - ownerRc.left) - (dialogRc.right - dialogRc.left)) / 2;
    int y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - (dialogRc.bottom - dialogRc.top)) / 2;
    SetWindowPos(dialog, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
    ShowWindow(dialog, SW_SHOW);
}

void updateListRow(int index, const Category& category) {
    ListView_SetCheckState(g_app.list, index, category.selected);
    std::wstring size = formatBytes(category.bytes);
    std::wstring files = std::to_wstring(category.files);
    ListView_SetItemText(g_app.list, index, 1, const_cast<LPWSTR>(size.c_str()));
    ListView_SetItemText(g_app.list, index, 2, const_cast<LPWSTR>(files.c_str()));
}

void refreshList() {
    ListView_DeleteAllItems(g_app.list);
    for (int i = 0; i < static_cast<int>(g_app.categories.size()); ++i) {
        const auto& c = g_app.categories[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<LPWSTR>(c.name.c_str());
        ListView_InsertItem(g_app.list, &item);
        updateListRow(i, c);
    }
}

void initCategories() {
    fs::path localAppData = getEnv(L"LOCALAPPDATA");
    fs::path appData = getEnv(L"APPDATA");
    fs::path userProfile = getEnv(L"USERPROFILE");
    fs::path programData = getEnv(L"PROGRAMDATA");
    fs::path downloads = knownFolder(FOLDERID_Downloads);
    fs::path documents = knownFolder(FOLDERID_Documents);
    if (documents.empty()) documents = userProfile / L"Documents";

    std::vector<fs::path> edge;
    addBrowserProfileCaches(edge, localAppData / L"Microsoft\\Edge\\User Data");

    std::vector<fs::path> chrome;
    addBrowserProfileCaches(chrome, localAppData / L"Google\\Chrome\\User Data");

    std::vector<fs::path> firefox;
    addFirefoxCaches(firefox, localAppData / L"Mozilla\\Firefox\\Profiles");

    std::vector<fs::path> wechat = {
        localAppData / L"Tencent\\WeChat\\Cache",
        localAppData / L"Tencent\\WeChat\\Temp",
        appData / L"Tencent\\WeChat\\Cache",
        appData / L"Tencent\\WeChat\\Temp"
    };
    addProfileSubdirs(wechat, documents / L"WeChat Files", {
        L"FileStorage\\Cache",
        L"FileStorage\\Temp",
        L"Applet\\Cache",
        L"Temp"
    });
    addProfileSubdirs(wechat, userProfile / L"WeChat Files", {
        L"FileStorage\\Cache",
        L"FileStorage\\Temp",
        L"Applet\\Cache",
        L"Temp"
    });

    std::vector<fs::path> qq = {
        localAppData / L"Tencent\\QQ\\Cache",
        localAppData / L"Tencent\\QQ\\Temp",
        localAppData / L"Tencent\\QQNT\\Cache",
        localAppData / L"Tencent\\QQNT\\Temp",
        appData / L"Tencent\\QQ\\Cache",
        appData / L"Tencent\\QQ\\Temp",
        appData / L"QQ\\nt_qq\\Cache",
        appData / L"QQ\\nt_qq\\Temp",
        appData / L"QQ\\nt_qq\\GPUCache",
        appData / L"QQ\\nt_qq\\Code Cache"
    };
    addProfileSubdirs(qq, appData / L"QQ\\nt_qq\\Partitions", {
        L"Cache",
        L"Code Cache",
        L"GPUCache",
        L"Service Worker\\CacheStorage"
    });

    std::vector<fs::path> qqBrowser;
    addBrowserProfileCaches(qqBrowser, localAppData / L"Tencent\\QQBrowser\\User Data");
    addBrowserProfileCaches(qqBrowser, appData / L"Tencent\\QQBrowser\\User Data");

    std::vector<fs::path> wps = {
        localAppData / L"Kingsoft\\WPS Office\\cache",
        localAppData / L"Kingsoft\\WPS Office\\Temp",
        localAppData / L"Kingsoft\\WPS Office\\WebCache",
        localAppData / L"Kingsoft\\WPS Cloud Files\\Cache",
        appData / L"Kingsoft\\office6\\cache",
        appData / L"Kingsoft\\office6\\temp",
        appData / L"kingsoft\\office6\\cache",
        appData / L"kingsoft\\office6\\temp",
        appData / L"Kingsoft\\WPS\\Cache"
    };

    std::vector<fs::path> office = {
        localAppData / L"Microsoft\\Office\\16.0\\OfficeFileCache",
        localAppData / L"Microsoft\\Office\\15.0\\OfficeFileCache",
        localAppData / L"Microsoft\\Office\\Wef",
        localAppData / L"Microsoft\\Office\\16.0\\Wef",
        localAppData / L"Microsoft\\Office\\15.0\\Wef",
        localAppData / L"Microsoft\\OneNote\\16.0\\cache",
        localAppData / L"Microsoft\\OneNote\\15.0\\cache",
        localAppData / L"Microsoft\\Windows\\INetCache\\Content.Outlook"
    };

    std::vector<fs::path> teams = {
        appData / L"Microsoft\\Teams\\Cache",
        appData / L"Microsoft\\Teams\\Code Cache",
        appData / L"Microsoft\\Teams\\GPUCache",
        appData / L"Microsoft\\Teams\\IndexedDB",
        appData / L"Microsoft\\Teams\\Service Worker\\CacheStorage",
        localAppData / L"Microsoft\\Teams\\Cache",
        localAppData / L"Microsoft\\Teams\\Code Cache",
        localAppData / L"Microsoft\\Teams\\GPUCache"
    };

    std::vector<fs::path> workApps = {
        appData / L"DingTalk\\Cache",
        appData / L"DingTalk\\GPUCache",
        appData / L"DingTalk\\Code Cache",
        localAppData / L"DingTalk\\Cache",
        localAppData / L"DingTalk\\GPUCache",
        appData / L"LarkShell\\Cache",
        appData / L"LarkShell\\GPUCache",
        appData / L"LarkShell\\Code Cache",
        appData / L"Feishu\\Cache",
        appData / L"Feishu\\GPUCache",
        appData / L"Feishu\\Code Cache",
        localAppData / L"Feishu\\Cache",
        appData / L"WXWork\\Cache",
        appData / L"WXWork\\Temp",
        appData / L"WXWork\\GPUCache",
        appData / L"WXWork\\Code Cache",
        localAppData / L"WXWork\\Cache",
        localAppData / L"WXWork\\Temp"
    };

    std::vector<fs::path> cloudApps = {
        appData / L"baidu\\BaiduNetdisk\\cache",
        appData / L"baidu\\BaiduNetdisk\\temp",
        localAppData / L"BaiduNetdisk\\Cache",
        localAppData / L"BaiduNetdisk\\Temp",
        localAppData / L"Microsoft\\OneDrive\\logs",
        localAppData / L"Microsoft\\OneDrive\\setup\\logs",
        appData / L"Dropbox\\Cache",
        localAppData / L"Dropbox\\Cache"
    };

    std::vector<fs::path> downloadApps = {
        appData / L"Thunder Network\\XLLiveUD\\Cache",
        appData / L"Thunder Network\\Thunder\\Profiles\\TaskDbDatBak",
        localAppData / L"Thunder Network\\Thunder\\Cache",
        localAppData / L"Thunder Network\\Thunder\\Temp",
        appData / L"qBittorrent\\cache",
        localAppData / L"qBittorrent\\cache",
        appData / L"uTorrent\\updates",
        appData / L"BitTorrent\\updates"
    };

    std::vector<fs::path> mediaApps = {
        localAppData / L"Netease\\CloudMusic\\Cache",
        localAppData / L"Netease\\CloudMusic\\webdata\\Cache",
        appData / L"Netease\\CloudMusic\\Cache",
        localAppData / L"Spotify\\Data",
        localAppData / L"Spotify\\Browser\\Cache",
        appData / L"Spotify\\Browser\\Cache",
        localAppData / L"iQIYI Video\\Cache",
        appData / L"iQIYI Video\\Cache",
        localAppData / L"Youku\\Cache",
        appData / L"Youku\\Cache",
        localAppData / L"Tencent\\QQLive\\Cache",
        appData / L"Tencent\\QQLive\\Cache"
    };

    std::vector<fs::path> gameApps = {
        localAppData / L"Steam\\htmlcache",
        localAppData / L"Steam\\widevine",
        appData / L"Steam\\htmlcache",
        localAppData / L"EpicGamesLauncher\\Saved\\webcache",
        localAppData / L"EpicGamesLauncher\\Saved\\webcache_4147",
        localAppData / L"EpicGamesLauncher\\Saved\\Logs",
        localAppData / L"Battle.net\\Cache",
        localAppData / L"Blizzard Entertainment\\Battle.net\\Cache",
        programData / L"Battle.net\\Cache",
        programData / L"Blizzard Entertainment\\Battle.net\\Cache",
        localAppData / L"miHoYoSDK\\Cache",
        localAppData / L"HoYoverse\\Cache"
    };

    std::vector<fs::path> creativeApps = {
        appData / L"Adobe\\Common\\Media Cache Files",
        appData / L"Adobe\\Common\\Media Cache",
        localAppData / L"Adobe\\Common\\Media Cache Files",
        localAppData / L"Adobe\\Common\\Media Cache",
        localAppData / L"Adobe\\OOBE\\Logs",
        appData / L"CapCut\\Cache",
        appData / L"CapCut\\GPUCache",
        appData / L"CapCut\\Code Cache",
        localAppData / L"JianyingPro\\Cache",
        appData / L"JianyingPro\\Cache",
        appData / L"obs-studio\\logs",
        localAppData / L"Blender Foundation\\Blender\\Cache"
    };

    std::vector<fs::path> utilityApps = {
        appData / L"360se6\\User Data\\Default\\Cache",
        appData / L"360se6\\User Data\\Default\\Code Cache",
        appData / L"360se6\\User Data\\Default\\GPUCache",
        localAppData / L"360Chrome\\Chrome\\User Data\\Default\\Cache",
        localAppData / L"360Chrome\\Chrome\\User Data\\Default\\Code Cache",
        localAppData / L"360Chrome\\Chrome\\User Data\\Default\\GPUCache",
        appData / L"Huorong\\Sysdiag\\Logs",
        programData / L"Huorong\\Sysdiag\\Logs",
        appData / L"7-Zip\\Temp",
        localAppData / L"Bandizip\\Temp",
        appData / L"WinRAR",
        localAppData / L"SogouPY\\Temp",
        localAppData / L"SogouPY\\Cache",
        appData / L"SogouPY\\Temp",
        appData / L"Baidu\\BaiduPinyin\\Cache",
        localAppData / L"Baidu\\BaiduPinyin\\Cache"
    };

    std::vector<fs::path> devCaches = {
        appData / L"Code\\Cache",
        appData / L"Code\\CachedData",
        appData / L"Code\\GPUCache",
        appData / L"Code\\Service Worker\\CacheStorage",
        appData / L"Cursor\\Cache",
        appData / L"Cursor\\CachedData",
        appData / L"Cursor\\GPUCache",
        appData / L"Cursor\\Service Worker\\CacheStorage",
        appData / L"JetBrains\\IntelliJIdea2024.1\\log",
        appData / L"JetBrains\\PyCharm2024.1\\log",
        appData / L"JetBrains\\WebStorm2024.1\\log",
        localAppData / L"JetBrains\\Transient",
        localAppData / L"Microsoft\\VisualStudio\\ComponentModelCache",
        localAppData / L"Microsoft\\VisualStudio\\Cache",
        localAppData / L"pip\\Cache",
        appData / L"npm-cache",
        localAppData / L"npm-cache",
        localAppData / L"Yarn\\Cache",
        localAppData / L"pnpm\\store",
        localAppData / L"Temp\\vite",
        localAppData / L"Temp\\webpack",
        localAppData / L"NuGet\\Cache",
        localAppData / L"Temp\\NuGetScratch"
    };

    std::vector<fs::path> systemCaches = {
        localAppData / L"D3DSCache",
        localAppData / L"NVIDIA\\DXCache",
        localAppData / L"NVIDIA\\GLCache",
        localAppData / L"AMD\\DxCache",
        programData / L"NVIDIA Corporation\\NV_Cache"
    };

    std::vector<fs::path> updateCaches = {
        L"C:\\Windows\\SoftwareDistribution\\Download",
        programData / L"Microsoft\\Windows\\DeliveryOptimization\\Cache"
    };

    g_app.categories = {
        {L"用户临时文件", {fs::temp_directory_path()}, true, true, 1, false},
        {L"Windows 临时文件", {L"C:\\Windows\\Temp"}, true, true, 1, false},
        {L"Windows 缩略图/错误报告缓存", {
            localAppData / L"Microsoft\\Windows\\Explorer",
            localAppData / L"Microsoft\\Windows\\WER"
        }, true, true, 1, false},
        {L"DirectX/NVIDIA/AMD 图形缓存", systemCaches, true, true, 1, false},
        {L"Microsoft Edge 缓存", edge, true, true, 1, false},
        {L"Chrome 缓存", chrome, true, true, 1, false},
        {L"Firefox 缓存", firefox, true, true, 1, false},
        {L"微信缓存", wechat, true, true, 1, false},
        {L"QQ/QQNT 缓存", qq, true, true, 1, false},
        {L"QQ 浏览器缓存", qqBrowser, true, true, 1, false},
        {L"WPS 缓存", wps, true, true, 1, false},
        {L"Microsoft 365/Office 缓存", office, true, true, 24, false},
        {L"Microsoft Teams 缓存", teams, true, true, 1, false},
        {L"钉钉/飞书/企业微信缓存", workApps, true, true, 1, false},
        {L"网盘缓存", cloudApps, true, true, 24, false},
        {L"下载工具缓存", downloadApps, true, true, 24, false},
        {L"影音软件缓存", mediaApps, true, true, 24, false},
        {L"游戏平台缓存", gameApps, false, false, 24, false},
        {L"Adobe/剪映/OBS 创作缓存", creativeApps, false, false, 24, false},
        {L"360/火绒/压缩/输入法缓存", utilityApps, true, true, 24, false},
        {L"开发工具缓存", devCaches, false, false, 24, false},
        {L"Windows 更新下载缓存", updateCaches, false, false, 24, false},
        {L"下载目录 30 天前文件", {downloads.empty() ? userProfile / L"Downloads" : downloads}, false, false, 24 * 30, false},
        {L"回收站", {}, false, false, 0, true}
    };
}

void scanPath(const fs::path& root, uintmax_t& bytes, uintmax_t& files, int minAgeHours) {
    std::error_code ec;
    if (root.empty() || !fs::exists(root, ec) || isProtectedPath(root)) return;
    if (fs::is_regular_file(root, ec)) {
        if (oldEnough(root, minAgeHours)) {
            uintmax_t size = fs::file_size(root, ec);
            if (!ec) {
                bytes += size;
                ++files;
            }
        }
        return;
    }
    if (!fs::is_directory(root, ec)) return;

    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        const fs::path p = it->path();
        if (isProtectedPath(p)) {
            it.disable_recursion_pending();
            it.increment(ec);
            continue;
        }
        if (it->is_regular_file(ec) && oldEnough(p, minAgeHours)) {
            uintmax_t size = it->file_size(ec);
            if (!ec) {
                bytes += size;
                ++files;
            }
        }
        it.increment(ec);
        ec.clear();
    }
}

void scanRecycleBin(Category& category) {
    SHQUERYRBINFO info{};
    info.cbSize = sizeof(info);
    if (SUCCEEDED(SHQueryRecycleBinW(L"C:\\", &info))) {
        category.bytes = static_cast<uintmax_t>(info.i64Size);
        category.files = static_cast<uintmax_t>(info.i64NumItems);
    }
}

void runScan() {
    if (g_app.busy.exchange(true)) return;
    PostMessageW(g_app.hwnd, WM_APP_PROGRESS, 0, 0);
    postString(WM_APP_STATUS, L"正在扫描...");
    postString(WM_APP_LOG, L"开始扫描可清理内容。");

    {
        std::lock_guard<std::mutex> lock(g_app.dataMutex);
        for (auto& c : g_app.categories) {
            c.bytes = 0;
            c.files = 0;
        }
    }

    for (size_t i = 0; i < g_app.categories.size(); ++i) {
        {
            std::lock_guard<std::mutex> lock(g_app.dataMutex);
            auto& category = g_app.categories[i];
            postString(WM_APP_LOG, L"扫描：" + category.name);
            if (category.recycleBin) {
                scanRecycleBin(category);
            } else {
                for (const auto& root : category.roots) {
                    scanPath(root, category.bytes, category.files, category.minAgeHours);
                }
            }
        }
        PostMessageW(g_app.hwnd, WM_APP_PROGRESS, static_cast<WPARAM>((i + 1) * 100 / g_app.categories.size()), 0);
    }

    uintmax_t total = 0;
    uintmax_t files = 0;
    {
        std::lock_guard<std::mutex> lock(g_app.dataMutex);
        for (const auto& c : g_app.categories) {
            total += c.bytes;
            files += c.files;
        }
    }
    postString(WM_APP_LOG, L"扫描完成。发现 " + formatBytes(total) + L"，文件数 " + std::to_wstring(files) + L"。");
    postString(WM_APP_STATUS, L"扫描完成：可清理 " + formatBytes(total));
    PostMessageW(g_app.hwnd, WM_APP_SCAN_DONE, 0, 0);
}

bool deletePath(const fs::path& p, uintmax_t& deletedFiles, uintmax_t& deletedBytes, int minAgeHours) {
    std::error_code ec;
    if (p.empty() || !fs::exists(p, ec) || isProtectedPath(p)) return false;
    if (fs::is_regular_file(p, ec)) {
        if (!oldEnough(p, minAgeHours)) return false;
        uintmax_t size = fs::file_size(p, ec);
        fs::remove(p, ec);
        if (!ec) {
            ++deletedFiles;
            deletedBytes += size;
            return true;
        }
        return false;
    }
    if (!fs::is_directory(p, ec)) return false;

    std::vector<fs::path> dirs;
    fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end;
    while (!ec && it != end) {
        fs::path current = it->path();
        if (isProtectedPath(current)) {
            it.disable_recursion_pending();
            it.increment(ec);
            continue;
        }
        if (it->is_regular_file(ec) && oldEnough(current, minAgeHours)) {
            uintmax_t size = it->file_size(ec);
            fs::remove(current, ec);
            if (!ec) {
                ++deletedFiles;
                deletedBytes += size;
            }
        } else if (it->is_directory(ec)) {
            dirs.push_back(current);
        }
        it.increment(ec);
        ec.clear();
    }
    std::sort(dirs.rbegin(), dirs.rend());
    for (const auto& dir : dirs) {
        if (!isProtectedPath(dir)) fs::remove(dir, ec);
        ec.clear();
    }
    return true;
}

void cleanRecycleBin(uintmax_t& files, uintmax_t& bytes) {
    SHQUERYRBINFO info{};
    info.cbSize = sizeof(info);
    if (SUCCEEDED(SHQueryRecycleBinW(L"C:\\", &info))) {
        bytes += static_cast<uintmax_t>(info.i64Size);
        files += static_cast<uintmax_t>(info.i64NumItems);
    }
    SHEmptyRecycleBinW(g_app.hwnd, L"C:\\", SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
}

void runClean() {
    if (g_app.busy.exchange(true)) return;
    postString(WM_APP_STATUS, L"正在清理...");
    postString(WM_APP_LOG, L"开始清理已勾选项目。");
    PostMessageW(g_app.hwnd, WM_APP_PROGRESS, 0, 0);

    uintmax_t deletedFiles = 0;
    uintmax_t deletedBytes = 0;
    std::vector<Category> selected;
    {
        std::lock_guard<std::mutex> lock(g_app.dataMutex);
        for (const auto& c : g_app.categories) {
            if (c.selected) selected.push_back(c);
        }
    }

    for (size_t i = 0; i < selected.size(); ++i) {
        const auto& category = selected[i];
        postString(WM_APP_LOG, L"清理：" + category.name);
        if (category.recycleBin) {
            cleanRecycleBin(deletedFiles, deletedBytes);
        } else {
            for (const auto& root : category.roots) {
                deletePath(root, deletedFiles, deletedBytes, category.minAgeHours);
            }
        }
        if (!selected.empty()) {
            PostMessageW(g_app.hwnd, WM_APP_PROGRESS, static_cast<WPARAM>((i + 1) * 100 / selected.size()), 0);
        }
    }

    postString(WM_APP_LOG, L"清理完成。删除约 " + formatBytes(deletedBytes) + L"，文件数 " + std::to_wstring(deletedFiles) + L"。");
    postString(WM_APP_STATUS, L"清理完成：删除约 " + formatBytes(deletedBytes));
    PostMessageW(g_app.hwnd, WM_APP_CLEAN_DONE, 0, 0);
}

void resizeLayout(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int margin = 28;
    int contentW = std::max(520, w - margin * 2);
    MoveWindow(g_app.title, margin, 22, contentW, 36, TRUE);
    MoveWindow(g_app.subtitle, margin, 58, contentW, 26, TRUE);

    int buttonTop = 98;
    MoveWindow(g_app.scanButton, margin, buttonTop, 112, 36, TRUE);
    MoveWindow(g_app.cleanButton, margin + 124, buttonTop, 112, 36, TRUE);
    MoveWindow(g_app.selectSafeButton, margin + 248, buttonTop, 150, 36, TRUE);
    MoveWindow(g_app.aboutButton, margin + 410, buttonTop, 92, 36, TRUE);
    MoveWindow(g_app.status, margin + 518, buttonTop + 8, std::max(120, w - margin - (margin + 518)), 24, TRUE);
    MoveWindow(g_app.progress, margin, buttonTop + 52, contentW, 8, TRUE);

    int listTop = 172;
    int logH = std::max(140, h / 4);
    int listH = std::max(230, h - listTop - logH - 34);
    int innerPad = 14;
    MoveWindow(g_app.list, margin + innerPad, listTop + innerPad, contentW - innerPad * 2, listH - innerPad * 2, TRUE);
    MoveWindow(g_app.log, margin + innerPad, listTop + listH + 14 + innerPad, contentW - innerPad * 2, logH - innerPad * 2, TRUE);
    applyRoundedRegions();

    int listW = contentW - innerPad * 2;
    ListView_SetColumnWidth(g_app.list, 0, std::max(240, listW - 300));
    ListView_SetColumnWidth(g_app.list, 1, 130);
    ListView_SetColumnWidth(g_app.list, 2, 110);
}

void createUi(HWND hwnd) {
    g_app.bgBrush = CreateSolidBrush(RGB_BG);
    g_app.panelBrush = CreateSolidBrush(RGB_PANEL);
    g_app.titleFont = makeFont(22, FW_SEMIBOLD);
    g_app.bodyFont = makeFont(10, FW_NORMAL);

    g_app.title = CreateWindowW(L"STATIC", L"磁盘清理 By MEMZGBL", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_TITLE), nullptr, nullptr);
    g_app.subtitle = CreateWindowW(L"STATIC", L"扫描并清理常见缓存、临时文件和应用缓存。清理前会要求确认。",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_SUBTITLE), nullptr, nullptr);

    g_app.scanButton = CreateWindowW(L"BUTTON", L"扫描", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_SCAN), nullptr, nullptr);
    g_app.cleanButton = CreateWindowW(L"BUTTON", L"清理", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_CLEAN), nullptr, nullptr);
    g_app.selectSafeButton = CreateWindowW(L"BUTTON", L"安全默认项", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_SELECT_SAFE), nullptr, nullptr);
    g_app.aboutButton = CreateWindowW(L"BUTTON", L"关于", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_ABOUT), nullptr, nullptr);
    g_app.status = CreateWindowW(L"STATIC", L"先扫描，再清理。下载目录、回收站、游戏/创作/开发/更新缓存默认不勾选。",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_STATUS), nullptr, nullptr);
    g_app.progress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_PROGRESS), nullptr, nullptr);

    g_app.list = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_LIST), nullptr, nullptr);
    ListView_SetExtendedListViewStyle(g_app.list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<LPWSTR>(L"项目");
    col.cx = 360;
    ListView_InsertColumn(g_app.list, 0, &col);
    col.pszText = const_cast<LPWSTR>(L"大小");
    col.cx = 130;
    ListView_InsertColumn(g_app.list, 1, &col);
    col.pszText = const_cast<LPWSTR>(L"文件数");
    col.cx = 110;
    ListView_InsertColumn(g_app.list, 2, &col);

    g_app.log = CreateWindowExW(0, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_LOG), nullptr, nullptr);

    applyFont(g_app.title, g_app.titleFont);
    applyFont(g_app.subtitle, g_app.bodyFont);
    applyFont(g_app.scanButton, g_app.bodyFont);
    applyFont(g_app.cleanButton, g_app.bodyFont);
    applyFont(g_app.selectSafeButton, g_app.bodyFont);
    applyFont(g_app.aboutButton, g_app.bodyFont);
    applyFont(g_app.status, g_app.bodyFont);
    applyFont(g_app.list, g_app.bodyFont);
    applyFont(g_app.log, g_app.bodyFont);

    SetWindowTheme(g_app.list, L"Explorer", nullptr);
    ListView_SetBkColor(g_app.list, RGB_PANEL);
    ListView_SetTextBkColor(g_app.list, RGB_PANEL);
    ListView_SetTextColor(g_app.list, RGB_TEXT);
    SendMessageW(g_app.list, LVM_SETICONSPACING, 0, MAKELPARAM(0, 34));

    SendMessageW(g_app.progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    initCategories();
    refreshList();
    resizeLayout(hwnd);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_app.hwnd = hwnd;
        createUi(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, g_app.bgBrush);

        int margin = 28;
        RECT topPanel{margin - 12, 88, rc.right - margin + 12, 160};
        int paintW = static_cast<int>(rc.right - rc.left);
        int paintH = static_cast<int>(rc.bottom - rc.top);
        int contentW = std::max(520, paintW - margin * 2);
        int listTop = 172;
        int logH = std::max(140, paintH / 4);
        int listH = std::max(230, paintH - listTop - logH - 34);
        RECT listPanel{margin, listTop, margin + contentW, listTop + listH};
        RECT logPanel{margin, listTop + listH + 14, margin + contentW, listTop + listH + 14 + logH};
        fillRoundRect(dc, topPanel, 12, RGB_PANEL, RGB(229, 229, 229));
        fillRoundRect(dc, listPanel, 18, RGB_PANEL, RGB_FRAME_BORDER);
        fillRoundRect(dc, logPanel, 18, RGB_PANEL, RGB_FRAME_BORDER);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        resizeLayout(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_DRAWITEM:
        if (wParam == IDC_SCAN || wParam == IDC_CLEAN || wParam == IDC_SELECT_SAFE || wParam == IDC_ABOUT) {
            drawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        SetBkMode(dc, TRANSPARENT);
        if (control == g_app.title) {
            SetTextColor(dc, RGB_TEXT);
            return reinterpret_cast<LRESULT>(g_app.bgBrush);
        }
        if (control == g_app.subtitle) {
            SetTextColor(dc, RGB_MUTED);
            return reinterpret_cast<LRESULT>(g_app.bgBrush);
        }
        SetTextColor(dc, RGB_MUTED);
        return reinterpret_cast<LRESULT>(g_app.panelBrush);
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, RGB_PANEL);
        SetTextColor(dc, RGB_TEXT);
        return reinterpret_cast<LRESULT>(g_app.panelBrush);
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_SCAN && !g_app.busy.load()) {
            setControlsEnabled(false);
            std::thread(runScan).detach();
        } else if (LOWORD(wParam) == IDC_CLEAN && !g_app.busy.load()) {
            int checked = 0;
            for (int i = 0; i < static_cast<int>(g_app.categories.size()); ++i) {
                g_app.categories[i].selected = ListView_GetCheckState(g_app.list, i);
                if (g_app.categories[i].selected) ++checked;
            }
            if (!checked) {
                MessageBoxW(hwnd, L"请先勾选要清理的项目。", L"磁盘清理 By MEMZGBL", MB_ICONINFORMATION);
                return 0;
            }
            int result = MessageBoxW(hwnd,
                L"将删除已勾选项目中的缓存和临时文件。请先关闭微信、QQ、WPS、Office、浏览器等相关程序以释放缓存文件。这个操作不能撤销，确定继续吗？",
                L"确认清理", MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON2);
            if (result == IDOK) {
                setControlsEnabled(false);
                std::thread(runClean).detach();
            }
        } else if (LOWORD(wParam) == IDC_SELECT_SAFE) {
            for (int i = 0; i < static_cast<int>(g_app.categories.size()); ++i) {
                g_app.categories[i].selected = g_app.categories[i].safeDefault;
                ListView_SetCheckState(g_app.list, i, g_app.categories[i].selected);
            }
        } else if (LOWORD(wParam) == IDC_ABOUT) {
            showAboutDialog(hwnd);
        }
        return 0;
    case WM_NOTIFY:
        if (reinterpret_cast<LPNMHDR>(lParam)->idFrom == IDC_LIST) {
            LPNMLISTVIEW item = reinterpret_cast<LPNMLISTVIEW>(lParam);
            if (item->hdr.code == LVN_ITEMCHANGED && item->iItem >= 0 && item->iItem < static_cast<int>(g_app.categories.size())) {
                g_app.categories[item->iItem].selected = ListView_GetCheckState(g_app.list, item->iItem);
            }
        }
        return 0;
    case WM_APP_LOG: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
        appendLog(*text);
        return 0;
    }
    case WM_APP_STATUS: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
        SetWindowTextW(g_app.status, text->c_str());
        return 0;
    }
    case WM_APP_PROGRESS:
        SendMessageW(g_app.progress, PBM_SETPOS, wParam, 0);
        return 0;
    case WM_APP_SCAN_DONE:
        {
            std::lock_guard<std::mutex> lock(g_app.dataMutex);
            refreshList();
        }
        g_app.busy.store(false);
        setControlsEnabled(true);
        return 0;
    case WM_APP_CLEAN_DONE:
        g_app.busy.store(false);
        setControlsEnabled(false);
        std::thread(runScan).detach();
        return 0;
    case WM_CLOSE:
        if (g_app.busy.load()) {
            MessageBoxW(hwnd, L"任务正在运行，请稍后关闭。", L"磁盘清理 By MEMZGBL", MB_ICONINFORMATION);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_app.titleFont) DeleteObject(g_app.titleFont);
        if (g_app.bodyFont) DeleteObject(g_app.bodyFont);
        if (g_app.bgBrush) DeleteObject(g_app.bgBrush);
        if (g_app.panelBrush) DeleteObject(g_app.panelBrush);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    const wchar_t className[] = L"CppDriveCleanerWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, className, L"磁盘清理 By MEMZGBL",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 660,
        nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
