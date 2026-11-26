#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>

#include <string>
#include <vector>

namespace lps::dialogs {

inline std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

inline std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

inline std::string openFileDialog(HWND owner, const wchar_t* title, const wchar_t* filter,
                                  const std::string& initialDir = "") {
    wchar_t fileBuf[4096] = {};
    std::wstring initW = utf8ToWide(initialDir);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = static_cast<DWORD>(sizeof(fileBuf) / sizeof(fileBuf[0]));
    ofn.lpstrTitle = title;
    ofn.lpstrInitialDir = initW.empty() ? nullptr : initW.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (GetOpenFileNameW(&ofn)) {
        return wideToUtf8(fileBuf);
    }
    return {};
}

inline std::string saveFileDialog(HWND owner, const wchar_t* title, const wchar_t* filter,
                                  const wchar_t* defExt, const std::string& defaultName = "",
                                  const std::string& initialDir = "") {
    wchar_t fileBuf[4096] = {};
    if (!defaultName.empty()) {
        std::wstring defW = utf8ToWide(defaultName);
        wcsncpy_s(fileBuf, defW.c_str(), _TRUNCATE);
    }
    std::wstring initW = utf8ToWide(initialDir);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = static_cast<DWORD>(sizeof(fileBuf) / sizeof(fileBuf[0]));
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = defExt;
    ofn.lpstrInitialDir = initW.empty() ? nullptr : initW.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (GetSaveFileNameW(&ofn)) {
        return wideToUtf8(fileBuf);
    }
    return {};
}

inline std::string openObjDialog(HWND owner) {
    return openFileDialog(
        owner,
        L"Open 3D Car Mesh (.obj / .fbx)",
        L"3D Car Meshes (*.obj;*.fbx)\0*.obj;*.fbx\0Wavefront OBJ (*.obj)\0*.obj\0Autodesk FBX (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0"
    );
}

inline std::string saveObjDialog(HWND owner, const std::string& defaultName = "edited_car.obj") {
    return saveFileDialog(
        owner,
        L"Export Edited 3D Mesh (.obj)",
        L"Wavefront OBJ (*.obj)\0*.obj\0All Files (*.*)\0*.*\0",
        L"obj",
        defaultName
    );
}

inline std::string openImageDialog(HWND owner, const wchar_t* title = L"Import Image (.png / .jpg)") {
    return openFileDialog(
        owner,
        title,
        L"Image Files (*.png;*.jpg;*.jpeg;*.bmp;*.tga)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga\0All Files (*.*)\0*.*\0"
    );
}

inline std::string savePngDialog(HWND owner, const std::string& defaultName = "livery_4k.png",
                                 const wchar_t* title = L"Export PNG") {
    return saveFileDialog(
        owner,
        title,
        L"PNG Image (*.png)\0*.png\0All Files (*.*)\0*.*\0",
        L"png",
        defaultName
    );
}

inline std::string openProjectDialog(HWND owner) {
    return openFileDialog(
        owner,
        L"Open Coatroom Project (.cr / .lps)",
        L"Coatroom Project (*.cr)\0*.cr\0Legacy Project (*.lps)\0*.lps\0All Files (*.*)\0*.*\0"
    );
}

inline std::string saveProjectDialog(HWND owner, const std::string& defaultName = "Untitled.cr") {
    return saveFileDialog(
        owner,
        L"Save Coatroom Project (.cr)",
        L"Coatroom Project (*.cr)\0*.cr\0Legacy Project (*.lps)\0*.lps\0All Files (*.*)\0*.*\0",
        L"cr",
        defaultName
    );
}

} // namespace lps::dialogs
