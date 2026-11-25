#pragma once

#include <functional>
#include <string>

namespace lps {

/// Metadata describing an available Coatroom release from release-manifest.json.
struct OtaReleaseInfo {
    std::string version;
    std::string downloadUrl;
    std::string sha256Hex;
    std::string exeUrl;
    std::string exeSha256Hex;
    std::string releaseNotes;
    std::string publishedAt;
};

/// Result of querying a remote OTA manifest URL.
struct OtaCheckResult {
    bool ok = false;
    bool updateAvailable = false;
    OtaReleaseInfo latest;
    std::string error;
};

/// Compare two semantic version strings (e.g. "1.2.0" vs "v1.3.0-rc1").
/// Returns -1 if a < b, 0 if a == b, +1 if a > b.
int compareSemver(const std::string& a, const std::string& b);

/// Parse a release-manifest.json payload into OtaReleaseInfo.
/// Validates required fields (version, download_url/downloadUrl, 64-hex sha256).
bool parseOtaManifestJson(const std::string& jsonText, OtaReleaseInfo& out, std::string& err);

/// Compute lowercase SHA-256 hex digest of a file using Windows CNG BCrypt.
/// Returns empty string on I/O or crypto failure.
std::string computeFileSha256Hex(const std::wstring& filePath);

/// Query manifestUrl over HTTPS (or http://127.0.0.1 for local tests) using WinHTTP
/// with WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY and User-Agent: Coatroom/1.2.0.
OtaCheckResult checkForOtaUpdates(const std::string& currentVersion, const std::string& manifestUrl);

/// Download rel.downloadUrl to destExePath via WinHTTP and verify exact SHA-256 match.
/// Rejects non-HTTPS URLs except http://127.0.0.1. Requires linking -lwinhttp -lbcrypt.
bool downloadAndVerifyOtaInstaller(
    const OtaReleaseInfo& rel,
    const std::wstring& destExePath,
    std::string& err,
    const std::function<void(float)>& onProgress = {});

/// Offline-safe self-test verifying compareSemver, parseOtaManifestJson, BCrypt SHA-256,
/// and non-HTTPS URL rejection. Suitable for --smoke-test.
bool runOtaSelfTest(std::string& report);

} // namespace lps
