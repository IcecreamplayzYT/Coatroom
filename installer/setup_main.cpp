#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kAppName[] = L"Coatroom";
constexpr wchar_t kAppVersion[] = L"1.2.0";
constexpr wchar_t kPublisher[] = L"Coatroom Studio";
constexpr wchar_t kWndClass[] = L"CoatroomSetupWizardClass";

// Dark neutral charcoal + Coatroom #2FE6CE accent palette
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
    IDC_TITLE = 1001,
    IDC_SUBTITLE = 1002,
    IDC_TOU_LABEL = 1003,
    IDC_TOU_EDIT = 1004,
    IDC_ACCEPT_CHECK = 1005,
    IDC_DIR_LABEL = 1006,
    IDC_DIR_EDIT = 1007,
    IDC_BROWSE_BTN = 1008,
    IDC_DESKTOP_CHECK = 1009,
    IDC_STARTMENU_CHECK = 1010,
    IDC_STATUS_LABEL = 1011,
    IDC_PROGRESS = 1012,
    IDC_INSTALL_BTN = 1013,
    IDC_CANCEL_BTN = 1014,
};

const char* kFallbackTermsUtf8 =
    "COATROOM TERMS OF USE AND SOFTWARE LICENSE AGREEMENT\r\n"
    "Version 1.2 - Effective 2026\r\n\r\n"
    "1. LICENSE GRANT\r\n"
    "Coatroom Studio grants you a non-exclusive, revocable, royalty-free license to install "
    "and use Coatroom on Windows systems you own or control for personal, educational, or "
    "commercial 3D automotive livery design and rendering.\r\n\r\n"
    "2. YOUR CONTENT & OWNERSHIP\r\n"
    "You retain 100% ownership of all .cr / .lps projects, exported 4K PNG liveries, and "
    "beauty renders you create with Coatroom.\r\n\r\n"
    "3. PRIVACY & NETWORK BEHAVIOR\r\n"
    "Coatroom is offline by default with zero telemetry. Optional update checks run only when "
    "you explicitly select Help > Check for Updates... via HTTPS + SHA-256 verification.\r\n\r\n"
    "4. WARRANTY DISCLAIMER\r\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.\r\n\r\n"
    "5. THIRD-PARTY CREDITS\r\n"
    "Built with Dear ImGui, stb, DirectX 11, Windows WinHTTP, and Windows CNG BCrypt.\r\n";

struct SetupOptions {
    bool testMode = false;
    bool updateMode = false;
    bool existingInstallDetected = false;
    bool acceptTermsCli = false;
    bool createDesktopShortcut = true;
    bool createStartMenuShortcut = true;
    bool skipRegistry = false;
    bool relaunchAfterUpdate = false;
    std::wstring existingVersion;
    std::wstring targetDir;
};

struct UiState {
    SetupOptions opts;
    HWND hwnd = nullptr;
    HWND hTouEdit = nullptr;
    HWND hAcceptCheck = nullptr;
    HWND hDirEdit = nullptr;
    HWND hBrowseBtn = nullptr;
    HWND hDesktopCheck = nullptr;
    HWND hStartMenuCheck = nullptr;
    HWND hStatus = nullptr;
    HWND hProgress = nullptr;
    HWND hInstallBtn = nullptr;
    HWND hCancelBtn = nullptr;

    HBRUSH brBg = nullptr;
    HBRUSH brSurface = nullptr;
    HFONT fontTitle = nullptr;
    HFONT fontNormal = nullptr;
    HFONT fontSmall = nullptr;
    HFONT fontMono = nullptr;

    bool installedOk = false;
    bool installing = false;
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

std::wstring getDefaultInstallDir() {
    wchar_t* localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)) && localAppData) {
        fs::path p = fs::path(localAppData) / L"Coatroom";
        CoTaskMemFree(localAppData);
        return p.wstring();
    }
    return expandEnvVars(L"%LOCALAPPDATA%\\Coatroom");
}

bool detectExistingInstallation(std::wstring& outDir, std::wstring& outVersion) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Coatroom",
                      0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t dirBuf[MAX_PATH] = {};
    DWORD dirBytes = sizeof(dirBuf);
    DWORD dirType = 0;
    LONG rcDir = RegQueryValueExW(hKey, L"InstallLocation", nullptr, &dirType, reinterpret_cast<BYTE*>(dirBuf), &dirBytes);

    wchar_t verBuf[64] = {};
    DWORD verBytes = sizeof(verBuf);
    DWORD verType = 0;
    LONG rcVer = RegQueryValueExW(hKey, L"DisplayVersion", nullptr, &verType, reinterpret_cast<BYTE*>(verBuf), &verBytes);
    RegCloseKey(hKey);

    if (rcDir != ERROR_SUCCESS || dirType != REG_SZ || dirBuf[0] == L'\0') {
        return false;
    }
    std::error_code ec;
    fs::path exePath = fs::path(dirBuf) / L"Coatroom.exe";
    if (!fs::exists(exePath, ec)) {
        return false;
    }
    outDir = dirBuf;
    outVersion = (rcVer == ERROR_SUCCESS && verType == REG_SZ && verBuf[0] != L'\0') ? verBuf : L"1.0";
    return true;
}

std::wstring normalizeNewlinesCrLf(const std::string& utf8) {
    if (utf8.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring wide(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), wlen);

    std::wstring out;
    out.reserve(wide.size() + 64);
    for (size_t i = 0; i < wide.size(); ++i) {
        if (wide[i] == L'\n' && (i == 0 || wide[i - 1] != L'\r')) {
            out.push_back(L'\r');
        }
        out.push_back(wide[i]);
    }
    return out;
}

fs::path findStagingDir(const fs::path& exeDir) {
    const std::vector<fs::path> candidates = {
        exeDir,
        exeDir / L"dist",
        exeDir / L"staging",
        exeDir / L"build",
        exeDir.parent_path() / L"build",
    };
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c / L"Coatroom.exe", ec)) {
            return c;
        }
    }
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c / L"LICENSE_AND_TOU.txt", ec) || fs::exists(c / L"assets", ec)) {
            return c;
        }
    }
    return exeDir;
}

std::wstring loadTermsText(const fs::path& exeDir, const fs::path& stageDir) {
    const std::vector<fs::path> paths = {
        stageDir / L"LICENSE_AND_TOU.txt",
        exeDir / L"LICENSE_AND_TOU.txt",
        exeDir.parent_path() / L"LICENSE_AND_TOU.txt",
    };
    for (const auto& p : paths) {
        std::error_code ec;
        if (fs::exists(p, ec)) {
            std::ifstream ifs(p, std::ios::binary);
            if (ifs) {
                std::ostringstream ss;
                ss << ifs.rdbuf();
                std::wstring w = normalizeNewlinesCrLf(ss.str());
                if (!w.empty()) return w;
            }
        }
    }
    return normalizeNewlinesCrLf(kFallbackTermsUtf8);
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

bool waitForUnlockedTarget(const fs::path& filePath, DWORD maxWaitMs) {
    std::error_code ec;
    if (!fs::exists(filePath, ec)) return true;

    DWORD waited = 0;
    while (waited < maxWaitMs) {
        HANDLE h = CreateFileW(
            filePath.c_str(),
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

bool copyLooseFile(const fs::path& src, const fs::path& dst, uint64_t& totalBytes, std::wstring& errOut) {
    std::error_code ec;
    if (!fs::exists(src, ec)) return true; // Optional asset not present
    if (pathsReferSame(src, dst)) {
        totalBytes += fs::file_size(dst, ec);
        return true;
    }
    if (dst.has_parent_path()) {
        fs::create_directories(dst.parent_path(), ec);
    }
    if (!waitForUnlockedTarget(dst, 5000)) {
        errOut = L"Target file is locked by a running Coatroom process: " + dst.wstring();
        return false;
    }
    if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
        errOut = L"CopyFileW failed for " + src.filename().wstring() + L" (error " + std::to_wstring(GetLastError()) + L")";
        return false;
    }
    totalBytes += fs::file_size(dst, ec);
    return true;
}

bool copyDirectoryRecursive(const fs::path& srcDir, const fs::path& dstDir, uint64_t& totalBytes, std::wstring& errOut) {
    std::error_code ec;
    if (!fs::exists(srcDir, ec) || !fs::is_directory(srcDir, ec)) return true;
    if (pathsReferSame(srcDir, dstDir)) return true;

    fs::create_directories(dstDir, ec);
    for (fs::recursive_directory_iterator it(srcDir, ec), end; it != end && !ec; it.increment(ec)) {
        fs::path rel = fs::relative(it->path(), srcDir, ec);
        if (ec) {
            errOut = L"Failed computing relative path under assets/";
            return false;
        }
        fs::path target = dstDir / rel;
        if (it->is_directory()) {
            fs::create_directories(target, ec);
        } else if (it->is_regular_file()) {
            if (!copyLooseFile(it->path(), target, totalBytes, errOut)) {
                return false;
            }
        }
    }
    return true;
}

bool writeRegSz(HKEY root, const wchar_t* subkey, const wchar_t* valueName, const std::wstring& data) {
    HKEY hKey = nullptr;
    LONG rc = RegCreateKeyExW(root, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr);
    if (rc != ERROR_SUCCESS) return false;
    const DWORD bytes = static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t));
    rc = RegSetValueExW(hKey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()), bytes);
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

bool writeRegDword(HKEY root, const wchar_t* subkey, const wchar_t* valueName, DWORD val) {
    HKEY hKey = nullptr;
    LONG rc = RegCreateKeyExW(root, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr);
    if (rc != ERROR_SUCCESS) return false;
    rc = RegSetValueExW(hKey, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val));
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

bool createShortcut(
    const fs::path& lnkPath,
    const fs::path& targetPath,
    const fs::path& workDir,
    const fs::path& iconPath,
    const std::wstring& args,
    const std::wstring& desc) {
    std::error_code ec;
    if (lnkPath.has_parent_path()) {
        fs::create_directories(lnkPath.parent_path(), ec);
    }
    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&psl));
    if (FAILED(hr) || !psl) return false;

    psl->SetPath(targetPath.c_str());
    psl->SetWorkingDirectory(workDir.c_str());
    if (!args.empty()) psl->SetArguments(args.c_str());
    if (!desc.empty()) psl->SetDescription(desc.c_str());
    if (fs::exists(iconPath, ec)) {
        psl->SetIconLocation(iconPath.c_str(), 0);
    } else {
        psl->SetIconLocation(targetPath.c_str(), 0);
    }

    IPersistFile* ppf = nullptr;
    hr = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
    bool ok = false;
    if (SUCCEEDED(hr) && ppf) {
        ok = SUCCEEDED(ppf->Save(lnkPath.c_str(), TRUE));
        ppf->Release();
    }
    psl->Release();
    return ok;
}

bool performInstall(
    const SetupOptions& opts,
    const std::function<void(int, const std::wstring&)>& onStep,
    std::wstring& errOut) {
    if (opts.targetDir.empty()) {
        errOut = L"Install directory cannot be empty.";
        return false;
    }

    const fs::path exePath(getExePath());
    const fs::path exeDir = exePath.parent_path();
    const fs::path stageDir = findStagingDir(exeDir);
    const fs::path targetDir(opts.targetDir);

    std::error_code ec;
    fs::create_directories(targetDir, ec);
    if (ec) {
        errOut = L"Cannot create target directory: " + targetDir.wstring();
        return false;
    }

    onStep(15, L"Copying Coatroom application files...");
    uint64_t totalBytes = 0;

    // Locate Coatroom.exe & CoatroomUninstall.exe from staging or sibling dir
    fs::path coatroomExeSrc = stageDir / L"Coatroom.exe";
    if (!fs::exists(coatroomExeSrc, ec) && !opts.testMode) {
        errOut = L"Coatroom.exe not found next to installer or in dist/staging folder.";
        return false;
    }
    if (!copyLooseFile(coatroomExeSrc, targetDir / L"Coatroom.exe", totalBytes, errOut)) return false;

    onStep(35, L"Copying updater, uninstaller, and icon assets...");
    fs::path updaterSrc = stageDir / L"CoatroomUpdater.exe";
    if (!fs::exists(updaterSrc, ec)) updaterSrc = exeDir / L"CoatroomUpdater.exe";
    if (!copyLooseFile(updaterSrc, targetDir / L"CoatroomUpdater.exe", totalBytes, errOut)) return false;

    fs::path uninstSrc = stageDir / L"CoatroomUninstall.exe";
    if (!fs::exists(uninstSrc, ec)) uninstSrc = exeDir / L"CoatroomUninstall.exe";
    if (!copyLooseFile(uninstSrc, targetDir / L"CoatroomUninstall.exe", totalBytes, errOut)) return false;

    fs::path icoSrc = stageDir / L"coatroom.ico";
    if (!fs::exists(icoSrc, ec)) icoSrc = exeDir / L"coatroom.ico";
    if (!fs::exists(icoSrc, ec)) icoSrc = stageDir / L"resources" / L"coatroom.ico";
    if (!fs::exists(icoSrc, ec)) icoSrc = exeDir.parent_path() / L"resources" / L"coatroom.ico";
    if (!copyLooseFile(icoSrc, targetDir / L"coatroom.ico", totalBytes, errOut)) return false;

    // Install LICENSE_AND_TOU.txt (fallback to embedded text if missing in staging)
    fs::path touSrc = stageDir / L"LICENSE_AND_TOU.txt";
    if (!fs::exists(touSrc, ec)) touSrc = exeDir / L"LICENSE_AND_TOU.txt";
    if (!fs::exists(touSrc, ec)) touSrc = exeDir.parent_path() / L"LICENSE_AND_TOU.txt";
    if (fs::exists(touSrc, ec)) {
        if (!copyLooseFile(touSrc, targetDir / L"LICENSE_AND_TOU.txt", totalBytes, errOut)) return false;
    } else {
        std::ofstream ofs(targetDir / L"LICENSE_AND_TOU.txt", std::ios::binary);
        if (ofs) {
            ofs << kFallbackTermsUtf8;
            totalBytes += std::strlen(kFallbackTermsUtf8);
        }
    }

    onStep(55, L"Copying assets folder...");
    fs::path assetsSrc = stageDir / L"assets";
    if (!fs::exists(assetsSrc, ec)) assetsSrc = exeDir / L"assets";
    if (!copyDirectoryRecursive(assetsSrc, targetDir / L"assets", totalBytes, errOut)) return false;

    if (!opts.skipRegistry) {
        onStep(75, L"Registering HKCU Uninstall entry and .cr file association...");
        const fs::path installedExe = targetDir / L"Coatroom.exe";
        const fs::path installedUninst = targetDir / L"CoatroomUninstall.exe";
        const fs::path installedIco = targetDir / L"coatroom.ico";
        const std::wstring iconLoc = fs::exists(installedIco, ec)
            ? installedIco.wstring()
            : (installedExe.wstring() + L",0");

        const wchar_t* kUninstKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Coatroom";
        writeRegSz(HKEY_CURRENT_USER, kUninstKey, L"DisplayName", kAppName);
        writeRegSz(HKEY_CURRENT_USER, kUninstKey, L"DisplayVersion", kAppVersion);
        writeRegSz(HKEY_CURRENT_USER, kUninstKey, L"Publisher", kPublisher);
        writeRegSz(HKEY_CURRENT_USER, kUninstKey, L"InstallLocation", targetDir.wstring());
        writeRegSz(HKEY_CURRENT_USER, kUninstKey, L"UninstallString", L"\"" + installedUninst.wstring() + L"\"");
        writeRegSz(HKEY_CURRENT_USER, kUninstKey, L"QuietUninstallString", L"\"" + installedUninst.wstring() + L"\" --quiet");
        writeRegSz(HKEY_CURRENT_USER, kUninstKey, L"DisplayIcon", iconLoc);
        writeRegDword(HKEY_CURRENT_USER, kUninstKey, L"NoModify", 1);
        writeRegDword(HKEY_CURRENT_USER, kUninstKey, L"NoRepair", 1);
        writeRegDword(HKEY_CURRENT_USER, kUninstKey, L"EstimatedSize", static_cast<DWORD>(std::max<uint64_t>(1, totalBytes / 1024)));

        writeRegSz(HKEY_CURRENT_USER, L"Software\\Classes\\.cr", nullptr, L"Coatroom.Project.1");
        writeRegSz(HKEY_CURRENT_USER, L"Software\\Classes\\Coatroom.Project.1", nullptr, L"Coatroom Livery Project");
        writeRegSz(HKEY_CURRENT_USER, L"Software\\Classes\\Coatroom.Project.1\\DefaultIcon", nullptr, iconLoc);
        writeRegSz(HKEY_CURRENT_USER, L"Software\\Classes\\Coatroom.Project.1\\shell\\open\\command", nullptr,
                   L"\"" + installedExe.wstring() + L"\" \"%1\"");

        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }

    onStep(90, L"Creating requested shortcuts...");
    const fs::path installedExe = targetDir / L"Coatroom.exe";
    const fs::path installedUninst = targetDir / L"CoatroomUninstall.exe";
    const fs::path installedIco = targetDir / L"coatroom.ico";

    if (opts.createDesktopShortcut) {
        wchar_t* desktopDir = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopDir)) && desktopDir) {
            createShortcut(
                fs::path(desktopDir) / L"Coatroom.lnk",
                installedExe,
                targetDir,
                installedIco,
                L"",
                L"Coatroom 3D Automotive Livery Studio");
            CoTaskMemFree(desktopDir);
        }
    }

    if (opts.createStartMenuShortcut) {
        wchar_t* programsDir = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programsDir)) && programsDir) {
            fs::path smDir = fs::path(programsDir) / L"Coatroom";
            createShortcut(
                smDir / L"Coatroom.lnk",
                installedExe,
                targetDir,
                installedIco,
                L"",
                L"Coatroom 3D Automotive Livery Studio");
            if (fs::exists(installedUninst, ec)) {
                createShortcut(
                    smDir / L"Uninstall Coatroom.lnk",
                    installedUninst,
                    targetDir,
                    installedIco,
                    L"",
                    L"Uninstall Coatroom");
            }
            CoTaskMemFree(programsDir);
        }
    }

    onStep(100, L"Coatroom installation complete.");
    return true;
}

void enableDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
    constexpr DWORD kDwmwaUseImmersiveDarkModeLegacy = 19;
    if (FAILED(DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkModeLegacy, &dark, sizeof(dark));
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

std::wstring pickInstallFolderDialog(HWND owner, const std::wstring& initial) {
    IFileOpenDialog* pfd = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void**>(&pfd))) || !pfd) {
        return {};
    }
    DWORD options = 0;
    pfd->GetOptions(&options);
    pfd->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    pfd->SetTitle(L"Select Coatroom Install Folder");

    if (!initial.empty()) {
        IShellItem* psiInit = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_IShellItem, reinterpret_cast<void**>(&psiInit))) && psiInit) {
            pfd->SetFolder(psiInit);
            psiInit->Release();
        }
    }

    std::wstring chosen;
    if (SUCCEEDED(pfd->Show(owner))) {
        IShellItem* psiResult = nullptr;
        if (SUCCEEDED(pfd->GetResult(&psiResult)) && psiResult) {
            PWSTR pszPath = nullptr;
            if (SUCCEEDED(psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath) {
                chosen = pszPath;
                CoTaskMemFree(pszPath);
            }
            psiResult->Release();
        }
    }
    pfd->Release();
    return chosen;
}

LRESULT CALLBACK SetupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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

        ui->fontTitle = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
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

        auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, DWORD exStyle,
                      int x, int y, int w, int h, int id, HFONT f) -> HWND {
            HWND c = CreateWindowExW(exStyle, cls, txt, WS_CHILD | WS_VISIBLE | style,
                                     x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                     GetModuleHandleW(nullptr), nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
            return c;
        };

        const wchar_t* titleText = ui->opts.existingInstallDetected
            ? L"Coatroom 1.2.0 — Update Existing Installation"
            : L"Coatroom 1.2.0 Setup";
        std::wstring subText = ui->opts.existingInstallDetected
            ? (L"Existing Coatroom v" + ui->opts.existingVersion + L" detected. Your .cr projects and settings will be preserved.")
            : L"3D Automotive Livery & Beauty Render Studio  (Per-User Install, No Admin Required)";

        mk(L"STATIC", titleText, 0, 0, 24, 18, 560, 28, IDC_TITLE, ui->fontTitle);
        mk(L"STATIC", subText.c_str(), 0, 0, 24, 48, 560, 20, IDC_SUBTITLE, ui->fontSmall);

        mk(L"STATIC", L"Terms of Use & Software License Agreement:", 0, 0, 24, 78, 400, 18, IDC_TOU_LABEL, ui->fontNormal);
        ui->hTouEdit = mk(L"EDIT", L"",
                          ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_BORDER,
                          0, 24, 100, 562, 165, IDC_TOU_EDIT, ui->fontMono);

        const fs::path exeDir = fs::path(getExePath()).parent_path();
        const fs::path stageDir = findStagingDir(exeDir);
        SetWindowTextW(ui->hTouEdit, loadTermsText(exeDir, stageDir).c_str());

        ui->hAcceptCheck = mk(L"BUTTON", L"I accept the Coatroom Terms of Use && Software License Agreement",
                              BS_AUTOCHECKBOX, 0, 24, 274, 562, 22, IDC_ACCEPT_CHECK, ui->fontNormal);

        mk(L"STATIC", L"Install Location:", 0, 0, 24, 308, 200, 18, IDC_DIR_LABEL, ui->fontNormal);
        ui->hDirEdit = mk(L"EDIT", ui->opts.targetDir.c_str(), ES_AUTOHSCROLL | WS_BORDER,
                          0, 24, 330, 464, 26, IDC_DIR_EDIT, ui->fontNormal);
        ui->hBrowseBtn = mk(L"BUTTON", L"Browse...", BS_OWNERDRAW, 0, 496, 330, 90, 26, IDC_BROWSE_BTN, ui->fontNormal);

        ui->hDesktopCheck = mk(L"BUTTON", L"Create Desktop shortcut", BS_AUTOCHECKBOX,
                               0, 24, 368, 240, 22, IDC_DESKTOP_CHECK, ui->fontNormal);
        SendMessageW(ui->hDesktopCheck, BM_SETCHECK, ui->opts.createDesktopShortcut ? BST_CHECKED : BST_UNCHECKED, 0);

        ui->hStartMenuCheck = mk(L"BUTTON", L"Create Start Menu shortcuts", BS_AUTOCHECKBOX,
                                 0, 280, 368, 260, 22, IDC_STARTMENU_CHECK, ui->fontNormal);
        SendMessageW(ui->hStartMenuCheck, BM_SETCHECK, ui->opts.createStartMenuShortcut ? BST_CHECKED : BST_UNCHECKED, 0);

        const wchar_t* initialStatus = ui->opts.existingInstallDetected
            ? L"Existing Coatroom installation found. Click Update to update in place."
            : L"Review and accept the Terms of Use to enable installation.";
        ui->hStatus = mk(L"STATIC", initialStatus, 0, 0, 24, 404, 562, 20, IDC_STATUS_LABEL, ui->fontSmall);
        ui->hProgress = mk(PROGRESS_CLASSW, L"", PBS_SMOOTH, 0, 24, 428, 562, 14, IDC_PROGRESS, ui->fontNormal);
        SendMessageW(ui->hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        ui->hCancelBtn = mk(L"BUTTON", L"Cancel", BS_OWNERDRAW, 0, 390, 456, 92, 32, IDC_CANCEL_BTN, ui->fontNormal);
        ui->hInstallBtn = mk(L"BUTTON", ui->opts.existingInstallDetected ? L"Update" : L"Install",
                             BS_OWNERDRAW, 0, 494, 456, 92, 32, IDC_INSTALL_BTN, ui->fontNormal);
        if (ui->opts.existingInstallDetected) {
            SendMessageW(ui->hAcceptCheck, BM_SETCHECK, BST_CHECKED, 0);
            EnableWindow(ui->hInstallBtn, TRUE);
        } else {
            EnableWindow(ui->hInstallBtn, FALSE);
        }
        return 0;
    }

    case WM_CTLCOLORDLG:
        return reinterpret_cast<LRESULT>(ui->brBg);

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND ctrl = reinterpret_cast<HWND>(lParam);
        int id = GetDlgCtrlID(ctrl);
        if (id == IDC_TOU_EDIT) {
            SetTextColor(hdc, kColText);
            SetBkColor(hdc, kColSurface);
            return reinterpret_cast<LRESULT>(ui->brSurface);
        }
        SetBkColor(hdc, kColBg);
        SetTextColor(hdc, (id == IDC_SUBTITLE || id == IDC_STATUS_LABEL) ? kColMuted : kColText);
        return reinterpret_cast<LRESULT>(ui->brBg);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, kColText);
        SetBkColor(hdc, kColSurface);
        return reinterpret_cast<LRESULT>(ui->brSurface);
    }

    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, kColBg);
        SetTextColor(hdc, kColText);
        return reinterpret_cast<LRESULT>(ui->brBg);
    }

    case WM_DRAWITEM: {
        const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (dis->CtlID == IDC_INSTALL_BTN) {
            drawFlatButton(dis, true, IsWindowEnabled(dis->hwndItem) != FALSE);
            return TRUE;
        }
        if (dis->CtlID == IDC_CANCEL_BTN || dis->CtlID == IDC_BROWSE_BTN) {
            drawFlatButton(dis, false, IsWindowEnabled(dis->hwndItem) != FALSE);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_ACCEPT_CHECK) {
            bool accepted = SendMessageW(ui->hAcceptCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (!ui->installedOk && !ui->installing) {
                EnableWindow(ui->hInstallBtn, accepted ? TRUE : FALSE);
                InvalidateRect(ui->hInstallBtn, nullptr, TRUE);
                SetWindowTextW(ui->hStatus, accepted ? L"Ready to install." : L"Review and accept the Terms of Use to enable installation.");
            }
            return 0;
        }
        if (id == IDC_BROWSE_BTN) {
            wchar_t cur[MAX_PATH] = {};
            GetWindowTextW(ui->hDirEdit, cur, MAX_PATH);
            std::wstring picked = pickInstallFolderDialog(hwnd, cur);
            if (!picked.empty()) {
                fs::path p(picked);
                if (_wcsicmp(p.filename().c_str(), L"Coatroom") != 0) {
                    p /= L"Coatroom";
                }
                SetWindowTextW(ui->hDirEdit, p.wstring().c_str());
            }
            return 0;
        }
        if (id == IDC_CANCEL_BTN) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (id == IDC_INSTALL_BTN) {
            if (ui->installedOk) {
                wchar_t dirBuf[MAX_PATH] = {};
                GetWindowTextW(ui->hDirEdit, dirBuf, MAX_PATH);
                fs::path exe = fs::path(dirBuf) / L"Coatroom.exe";
                if (fs::exists(exe)) {
                    ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, dirBuf, SW_SHOWNORMAL);
                }
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (SendMessageW(ui->hAcceptCheck, BM_GETCHECK, 0, 0) != BST_CHECKED) {
                MessageBoxW(hwnd, L"Please accept the Terms of Use before installing.", kAppName, MB_ICONWARNING);
                return 0;
            }

            wchar_t dirBuf[MAX_PATH] = {};
            GetWindowTextW(ui->hDirEdit, dirBuf, MAX_PATH);
            ui->opts.targetDir = expandEnvVars(dirBuf);
            ui->opts.createDesktopShortcut = (SendMessageW(ui->hDesktopCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            ui->opts.createStartMenuShortcut = (SendMessageW(ui->hStartMenuCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

            ui->installing = true;
            EnableWindow(ui->hInstallBtn, FALSE);
            EnableWindow(ui->hBrowseBtn, FALSE);

            std::wstring err;
            bool ok = performInstall(ui->opts, [&](int pct, const std::wstring& status) {
                SendMessageW(ui->hProgress, PBM_SETPOS, pct, 0);
                SetWindowTextW(ui->hStatus, status.c_str());
                UpdateWindow(hwnd);
            }, err);

            ui->installing = false;
            if (ok) {
                ui->installedOk = true;
                SetWindowTextW(ui->hInstallBtn, L"Launch");
                SetWindowTextW(ui->hCancelBtn, L"Close");
                EnableWindow(ui->hInstallBtn, TRUE);
            } else {
                EnableWindow(ui->hInstallBtn, TRUE);
                EnableWindow(ui->hBrowseBtn, TRUE);
                SetWindowTextW(ui->hStatus, err.c_str());
                MessageBoxW(hwnd, err.c_str(), L"Coatroom Setup Error", MB_ICONERROR);
            }
            InvalidateRect(ui->hInstallBtn, nullptr, TRUE);
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

SetupOptions parseCliOptions() {
    SetupOptions opts;
    opts.targetDir = getDefaultInstallDir();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return opts;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--test-install-dir" && i + 1 < argc) {
            opts.testMode = true;
            opts.targetDir = expandEnvVars(argv[++i]);
        } else if (arg.rfind(L"--test-install-dir=", 0) == 0) {
            opts.testMode = true;
            opts.targetDir = expandEnvVars(arg.substr(19));
        } else if (arg == L"/UPDATE" || arg == L"--update") {
            opts.updateMode = true;
        } else if (arg == L"--dir" && i + 1 < argc) {
            opts.targetDir = expandEnvVars(argv[++i]);
        } else if (arg.rfind(L"--dir=", 0) == 0) {
            opts.targetDir = expandEnvVars(arg.substr(6));
        } else if (arg == L"--accept-terms") {
            opts.acceptTermsCli = true;
        } else if (arg == L"--no-shortcuts") {
            opts.createDesktopShortcut = false;
            opts.createStartMenuShortcut = false;
        } else if (arg == L"--no-registry") {
            opts.skipRegistry = true;
        } else if (arg == L"--relaunch") {
            opts.relaunchAfterUpdate = true;
        }
    }
    LocalFree(argv);
    return opts;
}

} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    SetupOptions opts = parseCliOptions();

    // Non-interactive automated test install (--test-install-dir) or OTA handoff (/UPDATE --dir)
    if (opts.testMode || opts.updateMode) {
        attachParentConsoleIfPresent();
        if (opts.updateMode && !opts.testMode) {
            // OTA handoff preserves existing shortcut preference unless disabled
            opts.createDesktopShortcut = false;
            opts.createStartMenuShortcut = false;
        }
        std::wstring err;
        bool ok = performInstall(opts, [](int pct, const std::wstring& msg) {
            std::wprintf(L"[%3d%%] %ls\n", pct, msg.c_str());
        }, err);
        if (!ok) {
            std::fwprintf(stderr, L"[ERROR] %ls\n", err.c_str());
            CoUninitialize();
            return 1;
        }
        if (opts.updateMode && opts.relaunchAfterUpdate) {
            fs::path exe = fs::path(opts.targetDir) / L"Coatroom.exe";
            if (fs::exists(exe)) {
                ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, opts.targetDir.c_str(), SW_SHOWNORMAL);
            }
        }
        CoUninitialize();
        return 0;
    }

    UiState ui;
    ui.opts = opts;
    {
        std::wstring foundDir, foundVer;
        if (detectExistingInstallation(foundDir, foundVer)) {
            ui.opts.existingInstallDetected = true;
            ui.opts.existingVersion = foundVer;
            ui.opts.targetDir = foundDir;
            ui.opts.createDesktopShortcut = false;
            ui.opts.createStartMenuShortcut = false;
        }
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SetupWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT wr{0, 0, 610, 506};
    AdjustWindowRectEx(&wr, style, FALSE, 0);

    const int winW = wr.right - wr.left;
    const int winH = wr.bottom - wr.top;
    const int posX = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    const int posY = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    HWND hwnd = CreateWindowExW(
        0, kWndClass, L"Coatroom 1.2.0 Setup", style,
        posX, posY, winW, winH, nullptr, nullptr, hInstance, &ui);
    if (!hwnd) {
        CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    CoUninitialize();
    return 0;
}
