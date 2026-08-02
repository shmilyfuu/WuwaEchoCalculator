#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct NativeUpdateInfo {
    bool available = false;
    std::wstring version;
    std::wstring tag;
    std::wstring name;
    std::wstring notes;
    std::wstring source;
    std::wstring downloadUrl;
    std::wstring pageUrl;
    std::wstring sha256;
    std::wstring checksumUrl;
};

enum class NativeUpdatePhase {
    Idle,
    Checking,
    Available,
    UpToDate,
    Preparing,
    Downloading,
    Verifying,
    Ready,
    LaunchingInstaller,
    Cancelled,
    Error,
};

struct NativeUpdateSnapshot {
    NativeUpdatePhase phase = NativeUpdatePhase::Idle;
    bool manual = false;
    NativeUpdateInfo latest;
    std::wstring message;
    std::wstring error;
    std::wstring packagePath;
    std::uint64_t downloadedBytes = 0;
    std::uint64_t totalBytes = 0;
    std::uint64_t bytesPerSecond = 0;
};

struct NativeHttpUrlParts {
    std::wstring host;
    std::wstring path;
    std::uint16_t port = 0;
    bool secure = false;
};

bool ParseNativeHttpUrl(const std::wstring& url, NativeHttpUrlParts& parts, std::wstring& error);

class NativeUpdateManager {
public:
    using Callback = std::function<void(const NativeUpdateSnapshot&)>;

    NativeUpdateManager();
    ~NativeUpdateManager();
    NativeUpdateManager(const NativeUpdateManager&) = delete;
    NativeUpdateManager& operator=(const NativeUpdateManager&) = delete;

    bool Initialize(const std::wstring& executableDirectory,
                    const std::wstring& currentVersion,
                    Callback callback,
                    std::wstring& error);

    void Check(bool manual);
    void DownloadLatest();
    void CancelDownload();

    bool HasPreparedUpdate() const;
    std::wstring PreparedVersion() const;
    NativeUpdateInfo LatestInfo() const;

    bool LaunchPreparedInstaller(std::wstring& error);
    void ClearPreparedUpdate();

    static int CompareVersions(const std::wstring& left, const std::wstring& right);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
