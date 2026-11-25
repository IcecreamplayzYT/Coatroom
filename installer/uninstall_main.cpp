#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kWndClass[] = L"CoatroomUninstallClass";
constexpr COLORREF kColBg = RGB(0x22, 0x22, 0x22);
constexpr COLORREF kColBorder = RGB(0x3A, 0x3A, 0x3A);
constexpr COLORREF kColText = RGB(0xE8, 0xE8, 0xE8);
constexpr COLORREF kColMuted = RGB(0xA4, 0xA4, 0xA4);
constexpr COLORREF kColDanger = RGB(0xD9, 0x4A, 0x4A);
constexpr COLORREF kColBtnBg = RGB(0x30, 0x30, 0x30);

enum ControlId : int {
    IDC_TITLE = 2001,
    IDC_DESC = 2002,
    IDC_PATH_INFO = 2003,
    IDC_STATUS = 2004,
    IDC_UNINSTALL_BTN = 2005,
    IDC_CANCEL_BTN = 2006,
};

struct UninstallOptions {
    bool quiet = false;
    bool skipRegistry = false;
    std::wstring installDir;
};

struct UiState {
    UninstallOptions opts;
    HWND hwnd = nullptr;
    HWND hStatus = nullptr;
    HWND hUninstallBtn = nullptr;
    HWND hCancelBtn = nullptr;
    HBRUSH brBg = nullptr;
    HFONT fontTitle = nullptr;
    HFONT fontNormal = nullptr;
    HFONT fontSmall = nullptr;
    bool done = false;
};

void attachParentConsoleIfPresent() {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* dummy = nullptr;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        freopen_s(&dummy, "CONOUT$", "w", stderr);
    }
}

std::wstring getExePath() {
    std::wstring buf(32768, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    buf.resize(len);
    return buf;
}

std::wstring expandEnvVars(const std::wstring& input) {
    DWORD needed = ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
    if (needed == 0) return input;
    std::wstring out(needed, L'\0');
    DWORD written = ExpandEnvironmentStringsW(input.c_str(), out.data(), needed);
    if (written == 0) return input;
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

std::wstring readUninstallLocationFromRegistry() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Coatroom",
                      0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return {};
    }
    wchar_t buf[MAX_PATH] = {};
    DWORD bytes = sizeof(buf);
    DWORD type = 0;
    LONG rc = RegQueryValueExW(hKey, L"InstallLocation", nullptr, &type, reinterpret_cast<BYTE*>(buf), &bytes);
    RegCloseKey(hKey);
    if (rc == ERROR_SUCCESS && type == REG_SZ) {
        return std::wstring(buf);
    }
    return {};
}

bool pathsReferSame(const fs::path& a, const fs::path& b) {
    std::error_code ec1, ec2;
    fs::path ca = fs::weakly_canonical(a, ec1);
    fs::path cb = fs::weakly_canonical(b, ec2);
    if (!ec1 && !ec2) {
        return _wcsicmp(ca.c_str(), cb.c_str()) == 0;
    }
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

void scheduleSelfDelete(const fs::path& exePath, const fs::path& dirPath) {
    std::wstring cmd = L"cmd.exe /c ping 127.0.0.1 -n 2 > nul & del /f /q \"" +
                       exePath.wstring() + L"\" & rmdir \"" + dirPath.wstring() + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void removeFileIfExists(const fs::path& p) {
    std::error_code ec;
    if (fs::exists(p, ec)) {
        SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
        fs::remove(p, ec);
    }
}

void removeCrAssociationIfOwned() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.cr", 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        wchar_t val[128] = {};
        DWORD bytes = sizeof(val);
        DWORD type = 0;
        if (RegQueryValueExW(hKey, nullptr, nullptr, &type, reinterpret_cast<BYTE*>(val), &bytes) == ERROR_SUCCESS &&
            type == REG_SZ &&
            _wcsicmp(val, L"Coatroom.Project.1") == 0) {
            RegCloseKey(hKey);
            RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\.cr");
        } else {
            RegCloseKey(hKey);
        }
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Coatroom.Project.1");
}

bool performUninstall(const UninstallOptions& opts, std::wstring& statusOut) {
    const fs::path selfExe(getExePath());
    const fs::path targetDir(opts.installDir);
    std::error_code ec;

    // 1. Remove installed files
    const std::vector<const wchar_t*> files = {
        L"Coatroom.exe",
        L"Coatroom.exe.bak",
        L"Coatroom.exe.staging",
        L"CoatroomUpdater.exe",
        L"LICENSE_AND_TOU.txt",
        L"coatroom.ico",
    };
    for (const wchar_t* f : files) {
        removeFileIfExists(targetDir / f);
    }

    fs::path assetsDir = targetDir / L"assets";
    if (fs::exists(assetsDir, ec)) {
        fs::remove_all(assetsDir, ec);
    }

    // Remove CoatroomUninstall.exe if not currently running from that exact path
    fs::path uninstInTarget = targetDir / L"CoatroomUninstall.exe";
    bool selfInsideTarget = fs::exists(uninstInTarget, ec) && pathsReferSame(selfExe, uninstInTarget);
    if (!selfInsideTarget) {
        removeFileIfExists(uninstInTarget);
        if (fs::exists(targetDir, ec) && fs::is_empty(targetDir, ec)) {
            fs::remove(targetDir, ec);
        }
    }

    // 2. Remove Desktop & Start Menu shortcuts
    wchar_t* desktopDir = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopDir)) && desktopDir) {
        removeFileIfExists(fs::path(desktopDir) / L"Coatroom.lnk");
        CoTaskMemFree(desktopDir);
    }

    wchar_t* programsDir = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programsDir)) && programsDir) {
        fs::path smDir = fs::path(programsDir) / L"Coatroom";
        removeFileIfExists(smDir / L"Coatroom.lnk");
        removeFileIfExists(smDir / L"Uninstall Coatroom.lnk");
        if (fs::exists(smDir, ec) && fs::is_empty(smDir, ec)) {
            fs::remove(smDir, ec);
        }
        CoTaskMemFree(programsDir);
    }

    // 3. Remove HKCU registry keys
    if (!opts.skipRegistry) {
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Coatroom");
        removeCrAssociationIfOwned();
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }

    // 4. Schedule self-cleanup if running directly from installed directory
    if (selfInsideTarget) {
        scheduleSelfDelete(uninstInTarget, targetDir);
    }

    statusOut = L"Coatroom has been uninstalled.";
    return true;
}

void enableDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }
}

void drawFlatButton(const DRAWITEMSTRUCT* dis, bool isDanger) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;

    COLORREF fill = isDanger ? (pressed ? RGB(0xB8, 0x38, 0x38) : kColDanger)
                             : (pressed ? RGB(0x3D, 0x3D, 0x3D) : kColBtnBg);
    COLORREF borderCol = isDanger ? kColDanger : kColBorder;

    HBRUSH hBr = CreateSolidBrush(fill);
    FillRect(hdc, &rc, hBr);
    DeleteObject(hBr);

    HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(hPen);

    wchar_t text[128] = {};
    GetWindowTextW(dis->hwndItem, text, 128);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kColText);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK UninstallWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    UiState* ui = reinterpret_cast<UiState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        ui = reinterpret_cast<UiState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ui));
        ui->hwnd = hwnd;

        enableDarkTitleBar(hwnd);
        ui->brBg = CreateSolidBrush(kColBg);
        ui->fontTitle = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        ui->fontNormal = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        ui->fontSmall = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style,
                      int x, int y, int w, int h, int id, HFONT f) -> HWND {
            HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style,
                                     x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                     GetModuleHandleW(nullptr), nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
            return c;
        };

        mk(L"STATIC", L"Uninstall Coatroom 1.2.0", 0, 24, 20, 420, 26, IDC_TITLE, ui->fontTitle);
        mk(L"STATIC",
           L"This will remove Coatroom application files, Desktop/Start Menu shortcuts,\n"
           L"and per-user .cr file associations. Your saved livery projects are untouched.",
           0, 24, 54, 420, 38, IDC_DESC, ui->fontNormal);

        std::wstring locMsg = L"Directory: " + ui->opts.installDir;
        mk(L"STATIC", locMsg.c_str(), SS_PATHELLIPSIS, 24, 102, 420, 20, IDC_PATH_INFO, ui->fontSmall);

        ui->hStatus = mk(L"STATIC", L"Click Uninstall to proceed.", 0, 24, 130, 420, 20, IDC_STATUS, ui->fontSmall);
        ui->hCancelBtn = mk(L"BUTTON", L"Cancel", BS_OWNERDRAW, 248, 164, 92, 32, IDC_CANCEL_BTN, ui->fontNormal);
        ui->hUninstallBtn = mk(L"BUTTON", L"Uninstall", BS_OWNERDRAW, 352, 164, 92, 32, IDC_UNINSTALL_BTN, ui->fontNormal);
        return 0;
    }

    case WM_CTLCOLORDLG:
        return reinterpret_cast<LRESULT>(ui->brBg);

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
        SetBkColor(hdc, kColBg);
        SetTextColor(hdc, (id == IDC_PATH_INFO || id == IDC_STATUS) ? kColMuted : kColText);
        return reinterpret_cast<LRESULT>(ui->brBg);
    }

    case WM_DRAWITEM: {
        const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (dis->CtlID == IDC_UNINSTALL_BTN) {
            drawFlatButton(dis, !ui->done);
            return TRUE;
        }
        if (dis->CtlID == IDC_CANCEL_BTN) {
            drawFlatButton(dis, false);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_CANCEL_BTN) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (id == IDC_UNINSTALL_BTN) {
            if (ui->done) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            std::wstring status;
            performUninstall(ui->opts, status);
            ui->done = true;
            SetWindowTextW(ui->hStatus, status.c_str());
            SetWindowTextW(ui->hUninstallBtn, L"Close");
            EnableWindow(ui->hCancelBtn, FALSE);
            InvalidateRect(ui->hUninstallBtn, nullptr, TRUE);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
        if (ui) {
            if (ui->brBg) DeleteObject(ui->brBg);
            if (ui->fontTitle) DeleteObject(ui->fontTitle);
            if (ui->fontNormal) DeleteObject(ui->fontNormal);
            if (ui->fontSmall) DeleteObject(ui->fontSmall);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

UninstallOptions parseCliOptions() {
    UninstallOptions opts;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--quiet" || arg == L"-q" || arg == L"/S") {
                opts.quiet = true;
            } else if (arg == L"--dir" && i + 1 < argc) {
                opts.installDir = expandEnvVars(argv[++i]);
            } else if (arg.rfind(L"--dir=", 0) == 0) {
                opts.installDir = expandEnvVars(arg.substr(6));
            } else if (arg == L"--no-registry") {
                opts.skipRegistry = true;
            }
        }
        LocalFree(argv);
    }

    if (opts.installDir.empty()) {
        opts.installDir = readUninstallLocationFromRegistry();
    }
    if (opts.installDir.empty()) {
        opts.installDir = fs::path(getExePath()).parent_path().wstring();
    }
    return opts;
}

} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    UninstallOptions opts = parseCliOptions();

    if (opts.quiet) {
        attachParentConsoleIfPresent();
        std::wstring status;
        bool ok = performUninstall(opts, status);
        std::wprintf(L"%ls\n", status.c_str());
        CoUninitialize();
        return ok ? 0 : 1;
    }

    UiState ui;
    ui.opts = opts;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = UninstallWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT wr{0, 0, 468, 214};
    AdjustWindowRectEx(&wr, style, FALSE, 0);

    const int winW = wr.right - wr.left;
    const int winH = wr.bottom - wr.top;
    const int posX = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    const int posY = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    HWND hwnd = CreateWindowExW(
        0, kWndClass, L"Uninstall Coatroom", style,
        posX, posY, winW, winH, nullptr, nullptr, hInstance, &ui);
    if (!hwnd) {
        CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return 0;
}
