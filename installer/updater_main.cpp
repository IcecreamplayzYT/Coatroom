#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "app/ota_updater.hpp"

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kWndClass[] = L"CoatroomUpdaterClass";
constexpr char kDefaultManifestUrl[] =
    "https://github.com/IcecreamplayzYT/Coatroom/releases/latest/download/release-manifest.json";
constexpr char kDefaultVersion[] = "1.2.0";

constexpr COLORREF kColBg = RGB(0x22, 0x22, 0x22);
constexpr COLORREF kColSurface = RGB(0x1B, 0x1B, 0x1B);
constexpr COLORREF kColBorder = RGB(0x3A, 0x3A, 0x3A);
constexpr COLORREF kColText = RGB(0xE8, 0xE8, 0xE8);
constexpr COLORREF kColMuted = RGB(0xA4, 0xA4, 0xA4);
constexpr COLORREF kColAccent = RGB(0x2F, 0xE6, 0xCE);
constexpr COLORREF kColAccentDark = RGB(0x05, 0x22, 0x28);
constexpr COLORREF kColBtnBg = RGB(0x30, 0x30, 0x30);
constexpr COLORREF kColBtnDisabled = RGB(0x28, 0x28, 0x28);

enum ControlId : int {
    IDC_TITLE = 3001,
    IDC_SUBTITLE = 3002,
    IDC_NOTES_EDIT = 3003,
    IDC_STATUS = 3004,
    IDC_PROGRESS = 3005,
    IDC_UPDATE_BTN = 3006,
    IDC_CANCEL_BTN = 3007,
};

constexpr UINT WM_UPD_PROGRESS = WM_APP + 10;
constexpr UINT WM_UPD_CHECK_DONE = WM_APP + 11;
constexpr UINT WM_UPD_APPLY_DONE = WM_APP + 12;

struct UpdaterOptions {
    std::string manifestUrl = kDefaultManifestUrl;
    std::string currentVersion;
    std::wstring targetDir;
    DWORD waitPid = 0;
    bool relaunch = false;
    bool quiet = false;
    bool testMode = false;
    std::wstring testManifestArg;
    bool skipRegistry = false;
};

struct WorkerResult {
    bool ok = false;
    bool updateAvailable = false;
    lps::OtaReleaseInfo release;
    std::wstring message;
};

struct UiState {
    UpdaterOptions opts;
    lps::OtaReleaseInfo latest;
    bool hasAvailableUpdate = false;
    bool working = false;
    bool finished = false;

    HWND hwnd = nullptr;
    HWND hTitle = nullptr;
    HWND hSubtitle = nullptr;
    HWND hNotes = nullptr;
    HWND hStatus = nullptr;
    HWND hProgress = nullptr;
    HWND hUpdateBtn = nullptr;
    HWND hCancelBtn = nullptr;

    HBRUSH brBg = nullptr;
    HBRUSH brSurface = nullptr;
    HFONT fontTitle = nullptr;
    HFONT fontNormal = nullptr;
    HFONT fontSmall = nullptr;
    HFONT fontMono = nullptr;
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

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring utf8ToWideCrLf(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring w(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), needed);
    std::wstring out;
    out.reserve(w.size() + 32);
    for (size_t i = 0; i < w.size(); ++i) {
        if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) {
            out.push_back(L'\r');
        }
        out.push_back(w[i]);
    }
    return out;
}

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring w(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), needed);
    return w;
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

std::string readInstalledVersionFromRegistry() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Coatroom",
                      0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return {};
    }
    wchar_t buf[128] = {};
    DWORD bytes = sizeof(buf);
    DWORD type = 0;
    LONG rc = RegQueryValueExW(hKey, L"DisplayVersion", nullptr, &type, reinterpret_cast<BYTE*>(buf), &bytes);
    RegCloseKey(hKey);
    if (rc == ERROR_SUCCESS && type == REG_SZ) {
        return wideToUtf8(buf);
    }
    return {};
}

void updateInstalledVersionInRegistry(const std::string& newVersion) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Coatroom",
                      0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return;
    }
    std::wstring verW = utf8ToWide(newVersion);
    RegSetValueExW(hKey, L"DisplayVersion", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(verW.c_str()),
                   static_cast<DWORD>((verW.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
}

bool waitForProcessAndFileUnlock(DWORD pid, const fs::path& targetExe, DWORD maxWaitMs) {
    DWORD waited = 0;
    if (pid != 0) {
        HANDLE hProc = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (hProc) {
            DWORD waitRes = WaitForSingleObject(hProc, maxWaitMs);
            CloseHandle(hProc);
            if (waitRes == WAIT_TIMEOUT) {
                return false;
            }
        }
    }

    std::error_code ec;
    if (!fs::exists(targetExe, ec)) return true;

    while (waited < maxWaitMs) {
        HANDLE h = CreateFileW(
            targetExe.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            return true;
        }
        DWORD err = GetLastError();
        if (err != ERROR_SHARING_VIOLATION && err != ERROR_LOCK_VIOLATION) {
            return true;
        }
        Sleep(200);
        waited += 200;
    }
    return false;
}

bool isHttpOrHttpsUrl(const std::string& s) {
    std::string low = s;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return low.rfind("https://", 0) == 0 || low.rfind("http://", 0) == 0;
}

bool checkManifestSource(const UpdaterOptions& opts, lps::OtaReleaseInfo& outRel, bool& updateAvail, std::wstring& errOut) {
    if (opts.testMode && !opts.testManifestArg.empty() && !isHttpOrHttpsUrl(wideToUtf8(opts.testManifestArg))) {
        std::ifstream ifs(fs::path(opts.testManifestArg), std::ios::binary);
        if (!ifs) {
            errOut = L"Cannot open test manifest file: " + opts.testManifestArg;
            return false;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        std::string err;
        if (!lps::parseOtaManifestJson(ss.str(), outRel, err)) {
            errOut = utf8ToWide(err);
            return false;
        }
        updateAvail = (lps::compareSemver(outRel.version, opts.currentVersion) > 0);
        return true;
    }

    std::string url = opts.testMode && !opts.testManifestArg.empty()
        ? wideToUtf8(opts.testManifestArg)
        : opts.manifestUrl;
    lps::OtaCheckResult chk = lps::checkForOtaUpdates(opts.currentVersion, url);
    if (!chk.ok) {
        errOut = utf8ToWide(chk.error);
        return false;
    }
    outRel = std::move(chk.latest);
    updateAvail = chk.updateAvailable;
    return true;
}

bool downloadAndSwapExeInPlace(
    const UpdaterOptions& opts,
    const lps::OtaReleaseInfo& rel,
    const std::function<void(int, const std::wstring&)>& onStep,
    std::wstring& errOut) {
    const fs::path targetDir(opts.targetDir);
    const fs::path targetExe = targetDir / L"Coatroom.exe";
    const fs::path stagingExe = targetDir / L"Coatroom.exe.staging";
    const fs::path bakExe = targetDir / L"Coatroom.exe.bak";

    std::error_code ec;
    fs::create_directories(targetDir, ec);
    DeleteFileW(stagingExe.c_str());

    const bool hasDirectExe = !rel.exeUrl.empty() && !rel.exeSha256Hex.empty();
    const std::string payloadUrl = hasDirectExe ? rel.exeUrl : rel.downloadUrl;
    const std::string payloadSha = hasDirectExe ? rel.exeSha256Hex : rel.sha256Hex;

    onStep(10, L"Downloading Coatroom v" + utf8ToWide(rel.version) + L"...");

    if (opts.testMode && !isHttpOrHttpsUrl(payloadUrl)) {
        // Offline local-file test payload copy + BCrypt SHA-256 verification
        fs::path srcFile = utf8ToWide(payloadUrl);
        if (!fs::exists(srcFile, ec)) {
            errOut = L"Test payload file not found: " + srcFile.wstring();
            return false;
        }
        if (!CopyFileW(srcFile.c_str(), stagingExe.c_str(), FALSE)) {
            errOut = L"Failed staging test Coatroom.exe";
            return false;
        }
        std::string actualSha = lps::computeFileSha256Hex(stagingExe);
        if (actualSha.empty() || actualSha != payloadSha) {
            DeleteFileW(stagingExe.c_str());
            errOut = L"SHA-256 verification failed for staged test binary.";
            return false;
        }
        onStep(75, L"SHA-256 verified.");
    } else {
        lps::OtaReleaseInfo exeRel = rel;
        exeRel.downloadUrl = payloadUrl;
        exeRel.sha256Hex = payloadSha;

        std::string dlErr;
        if (!lps::downloadAndVerifyOtaInstaller(exeRel, stagingExe.wstring(), dlErr, [&](float p) {
                int pct = 10 + static_cast<int>(std::clamp(p, 0.0f, 1.0f) * 65.0f);
                onStep(pct, L"Downloading and verifying SHA-256...");
            })) {
            DeleteFileW(stagingExe.c_str());
            errOut = utf8ToWide(dlErr);
            return false;
        }
    }

    onStep(82, L"Waiting for running Coatroom process to exit...");
    if (!waitForProcessAndFileUnlock(opts.waitPid, targetExe, 10000)) {
        DeleteFileW(stagingExe.c_str());
        errOut = L"Coatroom.exe is still running/locked. Close Coatroom and retry.";
        return false;
    }

    onStep(90, L"Swapping Coatroom.exe atomically...");
    DeleteFileW(bakExe.c_str());

    bool hadExisting = fs::exists(targetExe, ec);
    if (hadExisting) {
        if (!MoveFileExW(targetExe.c_str(), bakExe.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(stagingExe.c_str());
            errOut = L"Failed backing up existing Coatroom.exe (error " + std::to_wstring(GetLastError()) + L")";
            return false;
        }
    }

    if (!MoveFileExW(stagingExe.c_str(), targetExe.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD moveErr = GetLastError();
        if (hadExisting) {
            MoveFileExW(bakExe.c_str(), targetExe.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
        DeleteFileW(stagingExe.c_str());
        errOut = L"Failed moving updated Coatroom.exe into place (error " + std::to_wstring(moveErr) + L")";
        return false;
    }

    DeleteFileW(bakExe.c_str());

    if (!opts.skipRegistry && !opts.testMode) {
        updateInstalledVersionInRegistry(rel.version);
    }

    onStep(100, L"Coatroom updated to v" + utf8ToWide(rel.version) + L"!");
    return true;
}

void enableDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }
}

void drawFlatButton(const DRAWITEMSTRUCT* dis, bool isPrimary, bool isEnabled) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;

    COLORREF fill = kColBtnBg;
    COLORREF textCol = kColText;
    COLORREF borderCol = kColBorder;

    if (!isEnabled) {
        fill = kColBtnDisabled;
        textCol = RGB(0x66, 0x66, 0x66);
        borderCol = RGB(0x30, 0x30, 0x30);
    } else if (isPrimary) {
        fill = pressed ? RGB(0x22, 0xB8, 0xA4) : kColAccent;
        textCol = kColAccentDark;
        borderCol = kColAccent;
    } else if (pressed) {
        fill = RGB(0x3D, 0x3D, 0x3D);
    }

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
    SetTextColor(hdc, textCol);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

struct ThreadParam {
    UiState* ui = nullptr;
    lps::OtaReleaseInfo rel;
};

DWORD WINAPI CheckThreadProc(LPVOID param) {
    auto* tp = reinterpret_cast<ThreadParam*>(param);
    UiState* ui = tp->ui;
    delete tp;

    auto* res = new WorkerResult();
    res->ok = checkManifestSource(ui->opts, res->release, res->updateAvailable, res->message);
    PostMessageW(ui->hwnd, WM_UPD_CHECK_DONE, 0, reinterpret_cast<LPARAM>(res));
    return 0;
}

DWORD WINAPI ApplyThreadProc(LPVOID param) {
    auto* tp = reinterpret_cast<ThreadParam*>(param);
    UiState* ui = tp->ui;
    lps::OtaReleaseInfo rel = tp->rel;
    delete tp;

    auto* res = new WorkerResult();
    res->release = rel;
    res->ok = downloadAndSwapExeInPlace(
        ui->opts,
        rel,
        [&](int pct, const std::wstring& status) {
            auto* msgText = new std::wstring(status);
            PostMessageW(ui->hwnd, WM_UPD_PROGRESS, static_cast<WPARAM>(pct), reinterpret_cast<LPARAM>(msgText));
        },
        res->message);
    PostMessageW(ui->hwnd, WM_UPD_APPLY_DONE, 0, reinterpret_cast<LPARAM>(res));
    return 0;
}

LRESULT CALLBACK UpdaterWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    UiState* ui = reinterpret_cast<UiState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        ui = reinterpret_cast<UiState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ui));
        ui->hwnd = hwnd;

        enableDarkTitleBar(hwnd);
        ui->brBg = CreateSolidBrush(kColBg);
        ui->brSurface = CreateSolidBrush(kColSurface);

        ui->fontTitle = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        ui->fontNormal = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        ui->fontSmall = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        ui->fontMono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style,
                      int x, int y, int w, int h, int id, HFONT f) -> HWND {
            HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style,
                                     x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                     GetModuleHandleW(nullptr), nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
            return c;
        };

        ui->hTitle = mk(L"STATIC", L"Coatroom Updater", 0, 24, 18, 460, 26, IDC_TITLE, ui->fontTitle);
        std::wstring sub = L"Current version: v" + utf8ToWide(ui->opts.currentVersion) +
                           L"  |  Folder: " + ui->opts.targetDir;
        ui->hSubtitle = mk(L"STATIC", sub.c_str(), SS_PATHELLIPSIS, 24, 48, 460, 18, IDC_SUBTITLE, ui->fontSmall);

        ui->hNotes = mk(L"EDIT", L"Checking for Coatroom updates...",
                        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_BORDER,
                        24, 76, 460, 128, IDC_NOTES_EDIT, ui->fontMono);

        ui->hStatus = mk(L"STATIC", L"Querying release manifest...", 0, 24, 214, 460, 18, IDC_STATUS, ui->fontSmall);
        ui->hProgress = mk(PROGRESS_CLASSW, L"", PBS_SMOOTH, 24, 236, 460, 14, IDC_PROGRESS, ui->fontNormal);
        SendMessageW(ui->hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        ui->hCancelBtn = mk(L"BUTTON", L"Close", BS_OWNERDRAW, 286, 264, 92, 32, IDC_CANCEL_BTN, ui->fontNormal);
        ui->hUpdateBtn = mk(L"BUTTON", L"Update", BS_OWNERDRAW, 392, 264, 92, 32, IDC_UPDATE_BTN, ui->fontNormal);
        EnableWindow(ui->hUpdateBtn, FALSE);

        ui->working = true;
        auto* tp = new ThreadParam{ui, {}};
        HANDLE hT = CreateThread(nullptr, 0, CheckThreadProc, tp, 0, nullptr);
        if (hT) CloseHandle(hT);
        return 0;
    }

    case WM_UPD_PROGRESS: {
        int pct = static_cast<int>(wParam);
        auto* text = reinterpret_cast<std::wstring*>(lParam);
        SendMessageW(ui->hProgress, PBM_SETPOS, pct, 0);
        if (text) {
            SetWindowTextW(ui->hStatus, text->c_str());
            delete text;
        }
        return 0;
    }

    case WM_UPD_CHECK_DONE: {
        auto* res = reinterpret_cast<WorkerResult*>(lParam);
        ui->working = false;
        if (res) {
            if (!res->ok) {
                SetWindowTextW(ui->hStatus, L"Update check failed.");
                SetWindowTextW(ui->hNotes, (L"Could not check for updates:\r\n" + res->message).c_str());
            } else if (res->updateAvailable) {
                ui->hasAvailableUpdate = true;
                ui->latest = res->release;
                std::wstring hdr = L"Update available: Coatroom v" + utf8ToWide(res->release.version);
                SetWindowTextW(ui->hTitle, hdr.c_str());
                SetWindowTextW(ui->hStatus, L"Ready to update Coatroom.exe in place.");
                std::wstring notes = L"Version: v" + utf8ToWide(res->release.version) + L"\r\n";
                if (!res->release.publishedAt.empty()) {
                    notes += L"Published: " + utf8ToWide(res->release.publishedAt) + L"\r\n";
                }
                notes += L"\r\n" + utf8ToWideCrLf(res->release.releaseNotes);
                SetWindowTextW(ui->hNotes, notes.c_str());
                EnableWindow(ui->hUpdateBtn, TRUE);
            } else {
                SetWindowTextW(ui->hStatus, L"Coatroom is up to date.");
                std::wstring msgText = L"You already have the latest version (v" +
                                       utf8ToWide(ui->opts.currentVersion) + L").";
                SetWindowTextW(ui->hNotes, msgText.c_str());
            }
            InvalidateRect(ui->hUpdateBtn, nullptr, TRUE);
            delete res;
        }
        return 0;
    }

    case WM_UPD_APPLY_DONE: {
        auto* res = reinterpret_cast<WorkerResult*>(lParam);
        ui->working = false;
        if (res) {
            if (res->ok) {
                ui->finished = true;
                SendMessageW(ui->hProgress, PBM_SETPOS, 100, 0);
                std::wstring doneMsg = L"Updated cleanly to Coatroom v" + utf8ToWide(res->release.version) + L".";
                SetWindowTextW(ui->hStatus, doneMsg.c_str());
                SetWindowTextW(ui->hUpdateBtn, L"Launch");
                EnableWindow(ui->hUpdateBtn, TRUE);
            } else {
                SetWindowTextW(ui->hStatus, L"Update failed.");
                SetWindowTextW(ui->hNotes, (L"Update error:\r\n" + res->message).c_str());
                EnableWindow(ui->hUpdateBtn, ui->hasAvailableUpdate ? TRUE : FALSE);
            }
            InvalidateRect(ui->hUpdateBtn, nullptr, TRUE);
            delete res;
        }
        return 0;
    }

    case WM_CTLCOLORDLG:
        return reinterpret_cast<LRESULT>(ui->brBg);

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND ctrl = reinterpret_cast<HWND>(lParam);
        int id = GetDlgCtrlID(ctrl);
        if (id == IDC_NOTES_EDIT) {
            SetTextColor(hdc, kColText);
            SetBkColor(hdc, kColSurface);
            return reinterpret_cast<LRESULT>(ui->brSurface);
        }
        SetBkColor(hdc, kColBg);
        SetTextColor(hdc, (id == IDC_SUBTITLE || id == IDC_STATUS) ? kColMuted : kColText);
        return reinterpret_cast<LRESULT>(ui->brBg);
    }

    case WM_DRAWITEM: {
        const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (dis->CtlID == IDC_UPDATE_BTN) {
            drawFlatButton(dis, true, IsWindowEnabled(dis->hwndItem) != FALSE);
            return TRUE;
        }
        if (dis->CtlID == IDC_CANCEL_BTN) {
            drawFlatButton(dis, false, IsWindowEnabled(dis->hwndItem) != FALSE);
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
        if (id == IDC_UPDATE_BTN) {
            if (ui->working) return 0;
            if (ui->finished) {
                fs::path exe = fs::path(ui->opts.targetDir) / L"Coatroom.exe";
                if (fs::exists(exe)) {
                    ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, ui->opts.targetDir.c_str(), SW_SHOWNORMAL);
                }
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (!ui->hasAvailableUpdate) return 0;
            ui->working = true;
            EnableWindow(ui->hUpdateBtn, FALSE);
            InvalidateRect(ui->hUpdateBtn, nullptr, TRUE);
            auto* tp = new ThreadParam{ui, ui->latest};
            HANDLE hT = CreateThread(nullptr, 0, ApplyThreadProc, tp, 0, nullptr);
            if (hT) CloseHandle(hT);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
        if (ui) {
            if (ui->brBg) DeleteObject(ui->brBg);
            if (ui->brSurface) DeleteObject(ui->brSurface);
            if (ui->fontTitle) DeleteObject(ui->fontTitle);
            if (ui->fontNormal) DeleteObject(ui->fontNormal);
            if (ui->fontSmall) DeleteObject(ui->fontSmall);
            if (ui->fontMono) DeleteObject(ui->fontMono);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

UpdaterOptions parseCliOptions() {
    UpdaterOptions opts;
    opts.targetDir = fs::path(getExePath()).parent_path().wstring();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--manifest" && i + 1 < argc) {
                opts.manifestUrl = wideToUtf8(argv[++i]);
            } else if (arg == L"--dir" && i + 1 < argc) {
                opts.targetDir = expandEnvVars(argv[++i]);
            } else if (arg == L"--pid" && i + 1 < argc) {
                opts.waitPid = static_cast<DWORD>(_wtol(argv[++i]));
            } else if (arg == L"--current-version" && i + 1 < argc) {
                opts.currentVersion = wideToUtf8(argv[++i]);
            } else if (arg == L"--relaunch") {
                opts.relaunch = true;
            } else if (arg == L"--quiet" || arg == L"-q") {
                opts.quiet = true;
            } else if (arg == L"--test-update" && i + 1 < argc) {
                opts.testMode = true;
                opts.quiet = true;
                opts.testManifestArg = argv[++i];
            } else if (arg == L"--test-dir" && i + 1 < argc) {
                opts.testMode = true;
                opts.quiet = true;
                opts.skipRegistry = true;
                opts.targetDir = expandEnvVars(argv[++i]);
            } else if (arg == L"--no-registry") {
                opts.skipRegistry = true;
            }
        }
        LocalFree(argv);
    }

    if (opts.currentVersion.empty()) {
        std::string regVer = readInstalledVersionFromRegistry();
        opts.currentVersion = regVer.empty() ? kDefaultVersion : regVer;
    }
    return opts;
}

} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    UpdaterOptions opts = parseCliOptions();

    if (opts.quiet || opts.testMode) {
        attachParentConsoleIfPresent();
        lps::OtaReleaseInfo rel;
        bool updateAvail = false;
        std::wstring err;
        if (!checkManifestSource(opts, rel, updateAvail, err)) {
            std::fwprintf(stderr, L"[UPDATER ERROR] %ls\n", err.c_str());
            return 1;
        }
        if (!updateAvail) {
            std::wprintf(L"[UPDATER] Already up to date (v%hs)\n", opts.currentVersion.c_str());
            return 0;
        }
        bool ok = downloadAndSwapExeInPlace(
            opts,
            rel,
            [](int pct, const std::wstring& status) {
                std::wprintf(L"[%3d%%] %ls\n", pct, status.c_str());
            },
            err);
        if (!ok) {
            std::fwprintf(stderr, L"[UPDATER ERROR] %ls\n", err.c_str());
            return 1;
        }
        if (opts.relaunch) {
            fs::path exe = fs::path(opts.targetDir) / L"Coatroom.exe";
            if (fs::exists(exe)) {
                ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, opts.targetDir.c_str(), SW_SHOWNORMAL);
            }
        }
        return 0;
    }

    UiState ui;
    ui.opts = opts;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = UpdaterWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT wr{0, 0, 508, 314};
    AdjustWindowRectEx(&wr, style, FALSE, 0);

    const int winW = wr.right - wr.left;
    const int winH = wr.bottom - wr.top;
    const int posX = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    const int posY = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    HWND hwnd = CreateWindowExW(
        0, kWndClass, L"Coatroom Updater", style,
        posX, posY, winW, winH, nullptr, nullptr, hInstance, &ui);
    if (!hwnd) {
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
