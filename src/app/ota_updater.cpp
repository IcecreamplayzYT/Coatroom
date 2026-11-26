#include "app/ota_updater.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace lps {

namespace {

constexpr wchar_t kUserAgent[] = L"Coatroom/1.3.0";
constexpr size_t kMaxManifestBytes = 2 * 1024 * 1024;
constexpr size_t kMaxInstallerBytes = 512 * 1024 * 1024;

std::string trimAscii(std::string s) {
    auto isWs = [](unsigned char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
    while (!s.empty() && isWs(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && isWs(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::string toLowerAscii(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

bool isValidSha256Hex(const std::string& hex) {
    if (hex.size() != 64) return false;
    for (char ch : hex) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
    }
    return true;
}

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

std::string formatWin32Error(const char* prefix, DWORD code) {
    std::ostringstream oss;
    oss << prefix << " (Win32 error " << code << ")";
    return oss.str();
}

struct ParsedSemver {
    int nums[3] = {0, 0, 0};
    std::vector<std::string> prerelease;
};

bool isAllDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char ch : s) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
    }
    return true;
}

ParsedSemver parseSemverParts(std::string s) {
    ParsedSemver out;
    s = trimAscii(std::move(s));
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) {
        s.erase(s.begin());
    }
    // Strip +build metadata
    size_t plusPos = s.find('+');
    if (plusPos != std::string::npos) {
        s.resize(plusPos);
    }
    // Separate -prerelease
    std::string core = s;
    size_t dashPos = s.find('-');
    if (dashPos != std::string::npos) {
        core = s.substr(0, dashPos);
        std::string pre = s.substr(dashPos + 1);
        size_t start = 0;
        while (start <= pre.size()) {
            size_t dot = pre.find('.', start);
            std::string tok = (dot == std::string::npos) ? pre.substr(start) : pre.substr(start, dot - start);
            if (!tok.empty()) out.prerelease.push_back(tok);
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
    }
    size_t start = 0;
    for (int idx = 0; idx < 3 && start <= core.size(); ++idx) {
        size_t dot = core.find('.', start);
        std::string tok = (dot == std::string::npos) ? core.substr(start) : core.substr(start, dot - start);
        if (!tok.empty()) {
            out.nums[idx] = std::atoi(tok.c_str());
        }
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return out;
}

// Minimal self-contained JSON value/parser for release-manifest.json
struct JsonVal {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    std::string str;
    std::vector<std::pair<std::string, JsonVal>> obj;
    std::vector<JsonVal> arr;

    const JsonVal* find(const char* key) const {
        if (kind != Kind::Object) return nullptr;
        for (const auto& kv : obj) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }
};

class MiniJsonParser {
public:
    explicit MiniJsonParser(const std::string& src) : s_(src) {}

    bool parseRoot(JsonVal& out, std::string& err) {
        // Strip UTF-8 BOM if present
        if (s_.size() >= 3 &&
            static_cast<unsigned char>(s_[0]) == 0xEF &&
            static_cast<unsigned char>(s_[1]) == 0xBB &&
            static_cast<unsigned char>(s_[2]) == 0xBF) {
            pos_ = 3;
        }
        skipWs();
        if (!parseValue(out, err)) return false;
        skipWs();
        if (pos_ < s_.size()) {
            err = "Unexpected trailing JSON content at byte " + std::to_string(pos_);
            return false;
        }
        return true;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;

    void skipWs() {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool parseValue(JsonVal& out, std::string& err) {
        skipWs();
        if (pos_ >= s_.size()) {
            err = "Unexpected end of JSON input";
            return false;
        }
        char c = s_[pos_];
        if (c == '{') return parseObject(out, err);
        if (c == '[') return parseArray(out, err);
        if (c == '"') {
            out.kind = JsonVal::Kind::String;
            return parseString(out.str, err);
        }
        if (c == 't' && s_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            out.kind = JsonVal::Kind::Bool;
            return true;
        }
        if (c == 'f' && s_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            out.kind = JsonVal::Kind::Bool;
            return true;
        }
        if (c == 'n' && s_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            out.kind = JsonVal::Kind::Null;
            return true;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return parseNumber(out, err);
        }
        err = std::string("Unexpected JSON character '") + c + "' at byte " + std::to_string(pos_);
        return false;
    }

    static void appendUtf8Codepoint(uint32_t cp, std::string& out) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool parseHex4(uint32_t& val, std::string& err) {
        if (pos_ + 4 > s_.size()) {
            err = "Truncated \\u escape in JSON string";
            return false;
        }
        val = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            val <<= 4;
            if (c >= '0' && c <= '9') val |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') val |= static_cast<uint32_t>(10 + c - 'a');
            else if (c >= 'A' && c <= 'F') val |= static_cast<uint32_t>(10 + c - 'A');
            else {
                err = "Invalid hex digit in \\u escape";
                return false;
            }
        }
        return true;
    }

    bool parseString(std::string& out, std::string& err) {
        if (pos_ >= s_.size() || s_[pos_] != '"') {
            err = "Expected '\"'";
            return false;
        }
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= s_.size()) {
                    err = "Unterminated escape in JSON string";
                    return false;
                }
                char esc = s_[pos_++];
                switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!parseHex4(cp, err)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 2 <= s_.size() &&
                        s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        uint32_t low = 0;
                        if (!parseHex4(low, err)) return false;
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                        }
                    }
                    appendUtf8Codepoint(cp, out);
                    break;
                }
                default:
                    err = std::string("Invalid JSON escape '\\") + esc + "'";
                    return false;
                }
            } else {
                out.push_back(c);
            }
        }
        err = "Unterminated JSON string";
        return false;
    }

    bool parseNumber(JsonVal& out, std::string& err) {
        size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
            err = "Invalid JSON number";
            return false;
        }
        while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            ++pos_;
            if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
                err = "Invalid JSON fraction";
                return false;
            }
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
                err = "Invalid JSON exponent";
                return false;
            }
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        out.kind = JsonVal::Kind::Number;
        out.str = s_.substr(start, pos_ - start);
        return true;
    }

    bool parseArray(JsonVal& out, std::string& err) {
        ++pos_; // '['
        out.kind = JsonVal::Kind::Array;
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            JsonVal elem;
            if (!parseValue(elem, err)) return false;
            out.arr.push_back(std::move(elem));
            skipWs();
            if (pos_ >= s_.size()) {
                err = "Unterminated JSON array";
                return false;
            }
            if (s_[pos_] == ']') {
                ++pos_;
                return true;
            }
            if (s_[pos_] != ',') {
                err = "Expected ',' or ']' in JSON array";
                return false;
            }
            ++pos_;
        }
    }

    bool parseObject(JsonVal& out, std::string& err) {
        ++pos_; // '{'
        out.kind = JsonVal::Kind::Object;
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skipWs();
            std::string key;
            if (!parseString(key, err)) return false;
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') {
                err = "Expected ':' after JSON object key";
                return false;
            }
            ++pos_;
            JsonVal val;
            if (!parseValue(val, err)) return false;
            out.obj.emplace_back(std::move(key), std::move(val));
            skipWs();
            if (pos_ >= s_.size()) {
                err = "Unterminated JSON object";
                return false;
            }
            if (s_[pos_] == '}') {
                ++pos_;
                return true;
            }
            if (s_[pos_] != ',') {
                err = "Expected ',' or '}' in JSON object";
                return false;
            }
            ++pos_;
        }
    }
};

struct CrackedOtaUrl {
    std::wstring host;
    std::wstring pathAndQuery;
    INTERNET_PORT port = 0;
    bool isHttps = false;
};

bool crackAndValidateOtaUrl(const std::string& url, CrackedOtaUrl& out, std::string& err) {
    if (url.empty()) {
        err = "Empty OTA URL";
        return false;
    }
    std::wstring urlW = utf8ToWide(url);
    if (urlW.empty()) {
        err = "Invalid UTF-8 in OTA URL";
        return false;
    }

    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(urlW.c_str(), static_cast<DWORD>(urlW.size()), 0, &uc)) {
        err = formatWin32Error("Invalid OTA URL", GetLastError());
        return false;
    }

    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    if (host.empty()) {
        err = "OTA URL is missing hostname";
        return false;
    }

    if (uc.nScheme == INTERNET_SCHEME_HTTPS) {
        out.isHttps = true;
    } else if (uc.nScheme == INTERNET_SCHEME_HTTP && host == L"127.0.0.1") {
        out.isHttps = false;
    } else {
        err = "Rejected insecure OTA URL (HTTPS is required; only http://127.0.0.1 is permitted for local tests)";
        return false;
    }

    out.host = std::move(host);
    out.port = uc.nPort;
    out.pathAndQuery.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength > 0 && uc.lpszExtraInfo) {
        out.pathAndQuery.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    }
    if (out.pathAndQuery.empty()) {
        out.pathAndQuery = L"/";
    }
    return true;
}

struct WinHttpHandle {
    HINTERNET h = nullptr;
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : h(handle) {}
    ~WinHttpHandle() {
        if (h) WinHttpCloseHandle(h);
    }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

bool openHttpGetRequest(
    const CrackedOtaUrl& target,
    WinHttpHandle& hSession,
    WinHttpHandle& hConnect,
    WinHttpHandle& hRequest,
    DWORD& statusCodeOut,
    uint64_t& contentLengthOut,
    std::string& err) {
    hSession.h = WinHttpOpen(
        kUserAgent,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession) {
        err = formatWin32Error("WinHttpOpen failed", GetLastError());
        return false;
    }

    WinHttpSetTimeouts(hSession.h, 5000, 8000, 10000, 20000);

    hConnect.h = WinHttpConnect(hSession.h, target.host.c_str(), target.port, 0);
    if (!hConnect) {
        err = formatWin32Error("WinHttpConnect failed", GetLastError());
        return false;
    }

    DWORD flags = target.isHttps ? WINHTTP_FLAG_SECURE : 0;
    hRequest.h = WinHttpOpenRequest(
        hConnect.h,
        L"GET",
        target.pathAndQuery.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (!hRequest) {
        err = formatWin32Error("WinHttpOpenRequest failed", GetLastError());
        return false;
    }

    if (!WinHttpSendRequest(
            hRequest.h,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)) {
        err = formatWin32Error("WinHttpSendRequest failed", GetLastError());
        return false;
    }

    if (!WinHttpReceiveResponse(hRequest.h, nullptr)) {
        err = formatWin32Error("WinHttpReceiveResponse failed", GetLastError());
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(
            hRequest.h,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX)) {
        err = formatWin32Error("WinHttpQueryHeaders(status) failed", GetLastError());
        return false;
    }

    statusCodeOut = statusCode;
    if (statusCode < 200 || statusCode >= 300) {
        err = "HTTP request failed with status " + std::to_string(statusCode);
        return false;
    }

    contentLengthOut = 0;
    wchar_t lenBuf[64] = {};
    DWORD lenBufSize = sizeof(lenBuf);
    if (WinHttpQueryHeaders(
            hRequest.h,
            WINHTTP_QUERY_CONTENT_LENGTH,
            WINHTTP_HEADER_NAME_BY_INDEX,
            lenBuf,
            &lenBufSize,
            WINHTTP_NO_HEADER_INDEX)) {
        contentLengthOut = static_cast<uint64_t>(_wcstoui64(lenBuf, nullptr, 10));
    }

    return true;
}

} // namespace

int compareSemver(const std::string& a, const std::string& b) {
    ParsedSemver pa = parseSemverParts(a);
    ParsedSemver pb = parseSemverParts(b);

    for (int i = 0; i < 3; ++i) {
        if (pa.nums[i] < pb.nums[i]) return -1;
        if (pa.nums[i] > pb.nums[i]) return 1;
    }

    const bool aPre = !pa.prerelease.empty();
    const bool bPre = !pb.prerelease.empty();
    if (!aPre && !bPre) return 0;
    if (!aPre && bPre) return 1;  // 1.2.0 > 1.2.0-rc1
    if (aPre && !bPre) return -1; // 1.2.0-rc1 < 1.2.0

    const size_t n = std::min(pa.prerelease.size(), pb.prerelease.size());
    for (size_t i = 0; i < n; ++i) {
        const std::string& sa = pa.prerelease[i];
        const std::string& sb = pb.prerelease[i];
        if (sa == sb) continue;
        const bool da = isAllDigits(sa);
        const bool db = isAllDigits(sb);
        if (da && db) {
            long long va = std::atoll(sa.c_str());
            long long vb = std::atoll(sb.c_str());
            if (va != vb) return va < vb ? -1 : 1;
        } else if (da != db) {
            // Numeric identifiers always have lower precedence than non-numeric
            return da ? -1 : 1;
        } else {
            return sa < sb ? -1 : 1;
        }
    }
    if (pa.prerelease.size() == pb.prerelease.size()) return 0;
    return pa.prerelease.size() < pb.prerelease.size() ? -1 : 1;
}

bool parseOtaManifestJson(const std::string& jsonText, OtaReleaseInfo& out, std::string& err) {
    JsonVal root;
    MiniJsonParser parser(jsonText);
    if (!parser.parseRoot(root, err)) {
        return false;
    }
    if (root.kind != JsonVal::Kind::Object) {
        err = "Manifest JSON root must be an object";
        return false;
    }

    auto readStrField = [&](std::initializer_list<const char*> keys) -> std::string {
        for (const char* k : keys) {
            const JsonVal* v = root.find(k);
            if (v && v->kind == JsonVal::Kind::String) {
                return v->str;
            }
        }
        return {};
    };

    OtaReleaseInfo parsed;
    parsed.version = trimAscii(readStrField({"version", "latest_version", "latestVersion"}));
    parsed.downloadUrl = trimAscii(readStrField({"download_url", "downloadUrl", "url"}));
    parsed.sha256Hex = toLowerAscii(trimAscii(readStrField({"sha256", "sha256Hex", "sha256_hex"})));
    parsed.exeUrl = trimAscii(readStrField({"exe_url", "exeUrl"}));
    parsed.exeSha256Hex = toLowerAscii(trimAscii(readStrField({"exe_sha256", "exeSha256", "exeSha256Hex", "exe_sha256_hex"})));
    parsed.releaseNotes = readStrField({"release_notes", "releaseNotes", "notes"});
    parsed.publishedAt = trimAscii(readStrField({"published_at", "publishedAt"}));

    if (parsed.version.empty()) {
        err = "Manifest JSON missing non-empty 'version' string";
        return false;
    }
    if (parsed.downloadUrl.empty()) {
        err = "Manifest JSON missing non-empty 'download_url' / 'downloadUrl' string";
        return false;
    }
    if (!isValidSha256Hex(parsed.sha256Hex)) {
        err = "Manifest JSON missing valid 64-character hex 'sha256' field";
        return false;
    }
    if (!parsed.exeUrl.empty() && !isValidSha256Hex(parsed.exeSha256Hex)) {
        err = "Manifest JSON has 'exe_url' without valid 64-character hex 'exe_sha256' field";
        return false;
    }
    if (!parsed.exeSha256Hex.empty() && !isValidSha256Hex(parsed.exeSha256Hex)) {
        err = "Manifest JSON has invalid 64-character hex 'exe_sha256' field";
        return false;
    }

    out = std::move(parsed);
    return true;
}

std::string computeFileSha256Hex(const std::wstring& filePath) {
    HANDLE hFile = CreateFileW(
        filePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return {};
    }

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    DWORD hashObjSize = 0;
    DWORD cbData = 0;
    DWORD hashLen = 0;
    std::vector<uint8_t> hashObj;
    std::vector<uint8_t> digest;
    std::string hexOut;

    auto cleanup = [&]() {
        if (hHash) BCryptDestroyHash(hHash);
        if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
        CloseHandle(hFile);
    };

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        cleanup();
        return {};
    }
    if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&hashObjSize), sizeof(DWORD), &cbData, 0) != 0 ||
        BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen), sizeof(DWORD), &cbData, 0) != 0 ||
        hashLen != 32) {
        cleanup();
        return {};
    }

    hashObj.resize(hashObjSize);
    digest.resize(hashLen);

    if (BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0) != 0) {
        cleanup();
        return {};
    }

    std::array<uint8_t, 65536> buf{};
    DWORD bytesRead = 0;
    BOOL readOk = FALSE;
    while ((readOk = ReadFile(hFile, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead, nullptr)) != FALSE && bytesRead > 0) {
        if (BCryptHashData(hHash, buf.data(), bytesRead, 0) != 0) {
            cleanup();
            return {};
        }
    }
    if (!readOk) {
        cleanup();
        return {};
    }

    if (BCryptFinishHash(hHash, digest.data(), hashLen, 0) != 0) {
        cleanup();
        return {};
    }

    static constexpr char kHex[] = "0123456789abcdef";
    hexOut.reserve(digest.size() * 2);
    for (uint8_t b : digest) {
        hexOut.push_back(kHex[(b >> 4) & 0x0F]);
        hexOut.push_back(kHex[b & 0x0F]);
    }

    cleanup();
    return hexOut;
}

OtaCheckResult checkForOtaUpdates(const std::string& currentVersion, const std::string& manifestUrl) {
    OtaCheckResult result;
    CrackedOtaUrl target;
    if (!crackAndValidateOtaUrl(manifestUrl, target, result.error)) {
        return result;
    }

    WinHttpHandle hSession, hConnect, hRequest;
    DWORD statusCode = 0;
    uint64_t contentLength = 0;
    if (!openHttpGetRequest(target, hSession, hConnect, hRequest, statusCode, contentLength, result.error)) {
        return result;
    }

    std::string body;
    if (contentLength > 0 && contentLength <= kMaxManifestBytes) {
        body.reserve(static_cast<size_t>(contentLength));
    }

    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hRequest.h, &avail)) {
        if (avail == 0) break;
        if (body.size() + avail > kMaxManifestBytes) {
            result.error = "OTA manifest exceeds maximum size limit (2 MB)";
            return result;
        }
        size_t oldSize = body.size();
        body.resize(oldSize + avail);
        DWORD readBytes = 0;
        if (!WinHttpReadData(hRequest.h, body.data() + oldSize, avail, &readBytes)) {
            result.error = formatWin32Error("WinHttpReadData failed", GetLastError());
            return result;
        }
        body.resize(oldSize + readBytes);
        if (readBytes == 0) break;
    }

    if (!parseOtaManifestJson(body, result.latest, result.error)) {
        return result;
    }

    result.ok = true;
    result.updateAvailable = (compareSemver(result.latest.version, currentVersion) > 0);
    return result;
}

bool downloadAndVerifyOtaInstaller(
    const OtaReleaseInfo& rel,
    const std::wstring& destExePath,
    std::string& err,
    const std::function<void(float)>& onProgress) {
    CrackedOtaUrl target;
    if (!crackAndValidateOtaUrl(rel.downloadUrl, target, err)) {
        return false;
    }

    std::string expectedSha = toLowerAscii(trimAscii(rel.sha256Hex));
    if (!isValidSha256Hex(expectedSha)) {
        err = "Release metadata does not contain a valid 64-character SHA-256 hex digest";
        return false;
    }
    if (destExePath.empty()) {
        err = "Destination installer path is empty";
        return false;
    }

    std::error_code ec;
    std::filesystem::path destFs(destExePath);
    if (destFs.has_parent_path()) {
        std::filesystem::create_directories(destFs.parent_path(), ec);
    }

    std::wstring partPath = destExePath + L".part";
    DeleteFileW(partPath.c_str());

    WinHttpHandle hSession, hConnect, hRequest;
    DWORD statusCode = 0;
    uint64_t contentLength = 0;
    if (!openHttpGetRequest(target, hSession, hConnect, hRequest, statusCode, contentLength, err)) {
        return false;
    }

    if (contentLength > kMaxInstallerBytes) {
        err = "OTA installer exceeds maximum allowed size (512 MB)";
        return false;
    }

    HANDLE hOut = CreateFileW(
        partPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (hOut == INVALID_HANDLE_VALUE) {
        err = formatWin32Error("Failed to create temporary installer file", GetLastError());
        return false;
    }

    if (onProgress) onProgress(0.0f);

    uint64_t totalRead = 0;
    DWORD avail = 0;
    bool ioFailed = false;
    std::vector<uint8_t> chunk;

    while (true) {
        if (!WinHttpQueryDataAvailable(hRequest.h, &avail)) {
            err = formatWin32Error("WinHttpQueryDataAvailable failed during download", GetLastError());
            ioFailed = true;
            break;
        }
        if (avail == 0) break;

        if (totalRead + avail > kMaxInstallerBytes) {
            err = "Downloaded OTA installer exceeds 512 MB safety limit";
            ioFailed = true;
            break;
        }

        chunk.resize(avail);
        DWORD bytesRead = 0;
        if (!WinHttpReadData(hRequest.h, chunk.data(), avail, &bytesRead)) {
            err = formatWin32Error("WinHttpReadData failed during download", GetLastError());
            ioFailed = true;
            break;
        }
        if (bytesRead == 0) break;

        DWORD written = 0;
        if (!WriteFile(hOut, chunk.data(), bytesRead, &written, nullptr) || written != bytesRead) {
            err = formatWin32Error("Failed writing downloaded installer chunk", GetLastError());
            ioFailed = true;
            break;
        }

        totalRead += bytesRead;
        if (onProgress && contentLength > 0) {
            float p = static_cast<float>(totalRead) / static_cast<float>(contentLength);
            onProgress(std::clamp(p, 0.0f, 0.99f));
        }
    }

    CloseHandle(hOut);

    if (ioFailed || totalRead == 0) {
        DeleteFileW(partPath.c_str());
        if (!ioFailed && totalRead == 0) {
            err = "Downloaded installer file was empty";
        }
        return false;
    }

    std::string actualSha = computeFileSha256Hex(partPath);
    if (actualSha.empty()) {
        DeleteFileW(partPath.c_str());
        err = "Failed to compute SHA-256 digest of downloaded installer";
        return false;
    }
    if (actualSha != expectedSha) {
        DeleteFileW(partPath.c_str());
        err = "SHA-256 verification failed! Expected " + expectedSha + ", got " + actualSha;
        return false;
    }

    if (!MoveFileExW(partPath.c_str(), destExePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        err = formatWin32Error("Failed to finalize verified installer file", GetLastError());
        DeleteFileW(partPath.c_str());
        return false;
    }

    if (onProgress) onProgress(1.0f);
    return true;
}

bool runOtaSelfTest(std::string& report) {
    std::ostringstream log;

    // 1. Semver comparison checks
    if (!(compareSemver("1.2.0", "1.1.9") > 0 &&
          compareSemver("v1.2.0", "1.2.0") == 0 &&
          compareSemver("1.2.0", "1.2.1") < 0 &&
          compareSemver("1.2.0", "1.2.0-rc1") > 0 &&
          compareSemver("1.10.0", "1.2.0") > 0)) {
        report = "OTA self-test failed: compareSemver assertions failed";
        return false;
    }
    log << "compareSemver=OK; ";

    // 2. JSON manifest parser checks
    const std::string sampleJson = R"({
        "version": "1.3.0",
        "download_url": "https://github.com/IcecreamplayzYT/Coatroom/releases/download/v1.3.0/CoatroomSetup.exe",
        "sha256": "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
        "exe_url": "https://github.com/IcecreamplayzYT/Coatroom/releases/download/v1.3.0/Coatroom.exe",
        "exe_sha256": "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
        "release_notes": "Line1\nLine2",
        "published_at": "2026-11-25T12:00:00Z"
    })";
    OtaReleaseInfo rel;
    std::string err;
    if (!parseOtaManifestJson(sampleJson, rel, err)) {
        report = "OTA self-test failed: parseOtaManifestJson valid case failed: " + err;
        return false;
    }
    if (rel.version != "1.3.0" ||
        rel.sha256Hex != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ||
        rel.exeUrl != "https://github.com/IcecreamplayzYT/Coatroom/releases/download/v1.3.0/Coatroom.exe" ||
        rel.exeSha256Hex != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ||
        rel.releaseNotes != "Line1\nLine2") {
        report = "OTA self-test failed: parsed manifest fields mismatch";
        return false;
    }
    OtaReleaseInfo badRel;
    if (parseOtaManifestJson(R"({"version":"1.3.0"})", badRel, err)) {
        report = "OTA self-test failed: invalid manifest unexpectedly succeeded";
        return false;
    }
    log << "parseOtaManifestJson=OK; ";

    // 3. BCrypt SHA-256 known NIST test vector ("abc") via temp file
    wchar_t tmpDir[MAX_PATH] = {};
    wchar_t tmpFile[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tmpDir) == 0 ||
        GetTempFileNameW(tmpDir, L"ota", 0, tmpFile) == 0) {
        report = "OTA self-test failed: could not allocate temp file";
        return false;
    }
    {
        HANDLE h = CreateFileW(tmpFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            DeleteFileW(tmpFile);
            report = "OTA self-test failed: could not open temp file for writing";
            return false;
        }
        const char vec[] = "abc";
        DWORD written = 0;
        BOOL ok = WriteFile(h, vec, 3, &written, nullptr);
        CloseHandle(h);
        if (!ok || written != 3) {
            DeleteFileW(tmpFile);
            report = "OTA self-test failed: failed writing test vector";
            return false;
        }
    }
    std::string sha = computeFileSha256Hex(tmpFile);
    DeleteFileW(tmpFile);
    if (sha != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
        report = "OTA self-test failed: BCrypt SHA-256 mismatch (got '" + sha + "')";
        return false;
    }
    log << "bcryptSha256=OK; ";

    // 4. Non-HTTPS URL rejection check (offline)
    OtaReleaseInfo insecureRel = rel;
    insecureRel.downloadUrl = "http://example.com/CoatroomSetup.exe";
    std::wstring dummyDest = std::wstring(tmpDir) + L"coatroom_ota_rejected.exe";
    if (downloadAndVerifyOtaInstaller(insecureRel, dummyDest, err)) {
        DeleteFileW(dummyDest.c_str());
        report = "OTA self-test failed: insecure http:// URL was not rejected";
        return false;
    }
    log << "httpsPolicy=OK";

    report = log.str();
    return true;
}

} // namespace lps
