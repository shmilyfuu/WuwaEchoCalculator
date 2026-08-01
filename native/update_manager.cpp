#define NOMINMAX
#include "update_manager.h"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace {

constexpr std::uint64_t kMaximumUpdateBytes = 200ull << 20;
constexpr wchar_t kGiteeLatestUrl[] = L"https://gitee.com/api/v5/repos/shmilyfuu/WuwaEchoCalculator/releases/latest";
constexpr wchar_t kGithubLatestUrl[] = L"https://api.github.com/repos/shmilyfuu/WuwaEchoCalculator/releases/latest";
constexpr wchar_t kGiteeAttachmentsFormat[] = L"https://gitee.com/api/v5/repos/shmilyfuu/WuwaEchoCalculator/releases/%lld/attach_files";
constexpr wchar_t kGiteePageBase[] = L"https://gitee.com/shmilyfuu/WuwaEchoCalculator";
constexpr wchar_t kGithubPageBase[] = L"https://github.com/shmilyfuu/WuwaEchoCalculator";

struct InternetHandle {
    HINTERNET value = nullptr;
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET handle) : value(handle) {}
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    InternetHandle(InternetHandle&& other) noexcept : value(other.value) { other.value = nullptr; }
    InternetHandle& operator=(InternetHandle&& other) noexcept {
        if (this != &other) {
            if (value) WinHttpCloseHandle(value);
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
    explicit operator bool() const { return value != nullptr; }
};

std::wstring ErrorText(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = length && buffer ? std::wstring(buffer, length) : L"Windows 错误 " + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) result.pop_back();
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), output.data(), count);
    return output;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string output(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), count, nullptr, nullptr);
    return output;
}

std::wstring Trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return value;
}

bool EqualsInsensitive(const std::wstring& left, const std::wstring& right) {
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

std::wstring QuoteArgument(const std::wstring& value) {
    if (value.find_first_of(L" \t\"") == std::wstring::npos) return value;
    std::wstring output = L"\"";
    std::size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') {
            output.append(slashes * 2 + 1, L'\\');
            output.push_back(L'\"');
            slashes = 0;
            continue;
        }
        output.append(slashes, L'\\');
        slashes = 0;
        output.push_back(ch);
    }
    output.append(slashes * 2, L'\\');
    output.push_back(L'\"');
    return output;
}

bool ParseVersion(const std::wstring& raw, std::array<int, 3>& numbers, std::wstring& prerelease) {
    std::wstring value = Trim(raw);
    if (!value.empty() && (value.front() == L'v' || value.front() == L'V')) value.erase(value.begin());
    const auto dash = value.find(L'-');
    prerelease = dash == std::wstring::npos ? L"" : ToLower(value.substr(dash + 1));
    const std::wstring core = dash == std::wstring::npos ? value : value.substr(0, dash);
    std::wstringstream stream(core);
    wchar_t dot1 = 0, dot2 = 0;
    if (!(stream >> numbers[0] >> dot1 >> numbers[1] >> dot2 >> numbers[2])) return false;
    if (dot1 != L'.' || dot2 != L'.') return false;
    wchar_t trailing = 0;
    if (stream >> trailing) return false;
    return numbers[0] >= 0 && numbers[1] >= 0 && numbers[2] >= 0;
}

std::size_t SkipWhitespace(const std::wstring& text, std::size_t position) {
    while (position < text.size() && iswspace(text[position])) ++position;
    return position;
}

bool ParseJsonString(const std::wstring& text, std::size_t position, std::wstring& output, std::size_t* endPosition = nullptr) {
    position = SkipWhitespace(text, position);
    if (position >= text.size() || text[position] != L'\"') return false;
    ++position;
    output.clear();
    while (position < text.size()) {
        wchar_t ch = text[position++];
        if (ch == L'\"') {
            if (endPosition) *endPosition = position;
            return true;
        }
        if (ch != L'\\') { output.push_back(ch); continue; }
        if (position >= text.size()) return false;
        const wchar_t escaped = text[position++];
        switch (escaped) {
        case L'\"': output.push_back(L'\"'); break;
        case L'\\': output.push_back(L'\\'); break;
        case L'/': output.push_back(L'/'); break;
        case L'b': output.push_back(L'\b'); break;
        case L'f': output.push_back(L'\f'); break;
        case L'n': output.push_back(L'\n'); break;
        case L'r': output.push_back(L'\r'); break;
        case L't': output.push_back(L'\t'); break;
        case L'u': {
            if (position + 4 > text.size()) return false;
            unsigned value = 0;
            for (int index = 0; index < 4; ++index) {
                const wchar_t digit = text[position++];
                value <<= 4;
                if (digit >= L'0' && digit <= L'9') value |= digit - L'0';
                else if (digit >= L'a' && digit <= L'f') value |= digit - L'a' + 10;
                else if (digit >= L'A' && digit <= L'F') value |= digit - L'A' + 10;
                else return false;
            }
            output.push_back(static_cast<wchar_t>(value));
            break;
        }
        default: return false;
        }
    }
    return false;
}

std::optional<std::size_t> FieldValuePosition(const std::wstring& object, const std::wstring& key) {
    const std::wstring needle = L"\"" + key + L"\"";
    std::size_t position = 0;
    while ((position = object.find(needle, position)) != std::wstring::npos) {
        position = SkipWhitespace(object, position + needle.size());
        if (position < object.size() && object[position] == L':') return SkipWhitespace(object, position + 1);
    }
    return std::nullopt;
}

std::wstring JsonStringField(const std::wstring& object, const std::wstring& key) {
    const auto position = FieldValuePosition(object, key);
    if (!position) return {};
    std::wstring output;
    return ParseJsonString(object, *position, output) ? output : std::wstring();
}

long long JsonIntegerField(const std::wstring& object, const std::wstring& key) {
    const auto position = FieldValuePosition(object, key);
    if (!position) return 0;
    std::size_t end = *position;
    if (end < object.size() && object[end] == L'-') ++end;
    while (end < object.size() && iswdigit(object[end])) ++end;
    if (end == *position) return 0;
    try { return std::stoll(object.substr(*position, end - *position)); } catch (...) { return 0; }
}

std::vector<std::wstring> JsonObjectsInRange(const std::wstring& text, std::size_t begin, std::size_t end) {
    std::vector<std::wstring> output;
    bool inString = false, escaped = false;
    int depth = 0;
    std::size_t objectStart = std::wstring::npos;
    for (std::size_t position = begin; position < end && position < text.size(); ++position) {
        const wchar_t ch = text[position];
        if (inString) {
            if (escaped) escaped = false;
            else if (ch == L'\\') escaped = true;
            else if (ch == L'\"') inString = false;
            continue;
        }
        if (ch == L'\"') { inString = true; continue; }
        if (ch == L'{') {
            if (depth++ == 0) objectStart = position;
        } else if (ch == L'}' && depth > 0) {
            if (--depth == 0 && objectStart != std::wstring::npos) {
                output.push_back(text.substr(objectStart, position - objectStart + 1));
                objectStart = std::wstring::npos;
            }
        }
    }
    return output;
}

std::vector<std::wstring> JsonArrayObjects(const std::wstring& object, const std::wstring& key) {
    const auto valuePosition = FieldValuePosition(object, key);
    if (!valuePosition) return {};
    std::size_t begin = SkipWhitespace(object, *valuePosition);
    if (begin >= object.size() || object[begin] != L'[') return {};
    bool inString = false, escaped = false;
    int depth = 0;
    for (std::size_t position = begin; position < object.size(); ++position) {
        const wchar_t ch = object[position];
        if (inString) {
            if (escaped) escaped = false;
            else if (ch == L'\\') escaped = true;
            else if (ch == L'\"') inString = false;
            continue;
        }
        if (ch == L'\"') { inString = true; continue; }
        if (ch == L'[') ++depth;
        else if (ch == L']' && --depth == 0) return JsonObjectsInRange(object, begin + 1, position);
    }
    return {};
}

bool OpenRequest(const std::wstring& url, InternetHandle& session, InternetHandle& connection,
                 InternetHandle& request, std::wstring& error) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    std::array<wchar_t, 256> host{};
    std::array<wchar_t, 4096> path{};
    components.lpszHostName = host.data(); components.dwHostNameLength = static_cast<DWORD>(host.size());
    components.lpszUrlPath = path.data(); components.dwUrlPathLength = static_cast<DWORD>(path.size());
    components.lpszExtraInfo = path.data();
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        error = L"解析更新地址失败：" + ErrorText(GetLastError());
        return false;
    }
    const std::wstring hostName(components.lpszHostName, components.dwHostNameLength);
    std::wstring requestPath(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength && components.lpszExtraInfo) requestPath.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    session = InternetHandle(WinHttpOpen(L"WuwaEchoCalculator-Updater/1.3.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { error = L"初始化网络组件失败：" + ErrorText(GetLastError()); return false; }
    WinHttpSetTimeouts(session.value, 10000, 10000, 15000, 120000);
    connection = InternetHandle(WinHttpConnect(session.value, hostName.c_str(), components.nPort, 0));
    if (!connection) { error = L"连接更新服务器失败：" + ErrorText(GetLastError()); return false; }
    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    request = InternetHandle(WinHttpOpenRequest(connection.value, L"GET", requestPath.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) { error = L"创建更新请求失败：" + ErrorText(GetLastError()); return false; }
    const wchar_t headers[] = L"Accept: application/json\r\n";
    if (!WinHttpAddRequestHeaders(request.value, headers, static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        error = L"设置更新请求失败：" + ErrorText(GetLastError()); return false;
    }
    if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) {
        error = L"请求更新信息失败：" + ErrorText(GetLastError()); return false;
    }
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        error = L"读取服务器状态失败：" + ErrorText(GetLastError()); return false;
    }
    if (status != 200) { error = L"更新服务器返回 HTTP " + std::to_wstring(status); return false; }
    return true;
}

bool HttpGet(const std::wstring& url, std::wstring& output, std::wstring& error, std::size_t maximumBytes = 8 << 20) {
    InternetHandle session, connection, request;
    if (!OpenRequest(url, session, connection, request, error)) return false;
    std::string bytes;
    std::array<char, 16384> buffer{};
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            error = L"读取更新信息失败：" + ErrorText(GetLastError()); return false;
        }
        if (!read) break;
        if (bytes.size() + read > maximumBytes) { error = L"更新信息超过允许大小"; return false; }
        bytes.append(buffer.data(), read);
    }
    output = Utf8ToWide(bytes);
    if (output.empty() && !bytes.empty()) { error = L"更新信息编码无效"; return false; }
    return true;
}

struct ReleaseAsset {
    std::wstring name;
    std::wstring url;
    std::wstring digest;
};

std::vector<ReleaseAsset> ParseAssets(const std::vector<std::wstring>& objects) {
    std::vector<ReleaseAsset> output;
    for (const auto& object : objects) {
        ReleaseAsset asset;
        asset.name = JsonStringField(object, L"name");
        asset.url = JsonStringField(object, L"browser_download_url");
        if (asset.url.empty()) asset.url = JsonStringField(object, L"download_url");
        asset.digest = JsonStringField(object, L"digest");
        if (!asset.name.empty() && !asset.url.empty()) output.push_back(std::move(asset));
    }
    return output;
}

bool FetchRelease(const std::wstring& source, const std::wstring& latestUrl, const std::wstring& pageBase,
                  bool gitee, const std::wstring& currentVersion, NativeUpdateInfo& info, std::wstring& error) {
    std::wstring body;
    if (!HttpGet(latestUrl, body, error, 2 << 20)) { error = source + L"：" + error; return false; }
    info = {};
    info.source = source;
    info.tag = JsonStringField(body, L"tag_name");
    info.version = info.tag;
    if (!info.version.empty() && (info.version.front() == L'v' || info.version.front() == L'V')) info.version.erase(info.version.begin());
    std::array<int, 3> parsed{}; std::wstring prerelease;
    if (!ParseVersion(info.version, parsed, prerelease)) { error = source + L" Release 版本号无效"; return false; }
    info.name = JsonStringField(body, L"name");
    info.notes = JsonStringField(body, L"body");
    info.pageUrl = JsonStringField(body, L"html_url");
    if (info.pageUrl.empty()) info.pageUrl = pageBase + L"/releases/tag/" + info.tag;
    std::vector<ReleaseAsset> assets = ParseAssets(JsonArrayObjects(body, L"assets"));
    auto attached = ParseAssets(JsonArrayObjects(body, L"attach_files"));
    assets.insert(assets.end(), attached.begin(), attached.end());
    if (gitee) {
        const long long releaseId = JsonIntegerField(body, L"id");
        if (releaseId > 0) {
            wchar_t attachmentUrl[512]{};
            swprintf_s(attachmentUrl, kGiteeAttachmentsFormat, releaseId);
            std::wstring attachmentBody, attachmentError;
            if (HttpGet(attachmentUrl, attachmentBody, attachmentError, 2 << 20)) {
                auto additional = ParseAssets(JsonObjectsInRange(attachmentBody, 0, attachmentBody.size()));
                assets.insert(assets.end(), additional.begin(), additional.end());
            }
        }
    }
    const std::wstring expected = L"WuwaEchoCalculator-v" + info.version + L"-windows-x64.zip";
    for (const auto& asset : assets) {
        if (EqualsInsensitive(asset.name, expected)) {
            info.downloadUrl = asset.url;
            std::wstring digest = ToLower(Trim(asset.digest));
            if (digest.rfind(L"sha256:", 0) == 0) digest.erase(0, 7);
            info.sha256 = digest;
        } else if (EqualsInsensitive(asset.name, expected + L".sha256")) {
            info.checksumUrl = asset.url;
        }
    }
    if (info.downloadUrl.empty()) { error = source + L" Release 缺少 " + expected; return false; }
    info.available = NativeUpdateManager::CompareVersions(currentVersion, info.version) < 0;
    return true;
}

bool CheckLatest(const std::wstring& currentVersion, NativeUpdateInfo& output, std::wstring& error) {
    NativeUpdateInfo gitee, github;
    std::wstring giteeError, githubError;
    const bool giteeOk = FetchRelease(L"Gitee", kGiteeLatestUrl, kGiteePageBase, true, currentVersion, gitee, giteeError);
    if (giteeOk && gitee.available) { output = gitee; return true; }
    const bool githubOk = FetchRelease(L"GitHub", kGithubLatestUrl, kGithubPageBase, false, currentVersion, github, githubError);
    if (githubOk) {
        if (!giteeOk || NativeUpdateManager::CompareVersions(gitee.version, github.version) < 0) output = github;
        else output = gitee;
        return true;
    }
    if (giteeOk) { output = gitee; return true; }
    error = giteeError + L"；" + githubError;
    return false;
}

std::wstring HexDigest(const std::array<unsigned char, 32>& digest) {
    std::wstringstream stream;
    stream << std::hex << std::setfill(L'0');
    for (unsigned char byte : digest) stream << std::setw(2) << static_cast<unsigned>(byte);
    return stream.str();
}

bool IsHexDigest(const std::wstring& value) {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](wchar_t ch) { return iswxdigit(ch) != 0; });
}

bool FetchChecksum(const std::wstring& url, std::wstring& checksum, std::wstring& error) {
    std::wstring body;
    if (!HttpGet(url, body, error, 4096)) return false;
    std::wstringstream stream(body);
    stream >> checksum;
    checksum = ToLower(Trim(checksum));
    if (!IsHexDigest(checksum)) { error = L"更新包校验文件格式无效"; return false; }
    return true;
}

bool WriteMarker(const std::filesystem::path& path, const NativeUpdateInfo& info,
                 const std::filesystem::path& package, std::wstring& error) {
    const auto temporary = path.wstring() + L".tmp";
    std::ofstream file(std::filesystem::path(temporary), std::ios::binary | std::ios::trunc);
    if (!file) { error = L"无法写入待安装更新标记"; return false; }
    file << "version=" << WideToUtf8(info.version) << "\n";
    file << "source=" << WideToUtf8(info.source) << "\n";
    file << "sha256=" << WideToUtf8(info.sha256) << "\n";
    file << "package=" << WideToUtf8(package.wstring()) << "\n";
    file.close();
    if (!file) { error = L"写入待安装更新标记失败"; return false; }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::rename(temporary, path, ec);
    if (ec) { error = L"保存待安装更新标记失败"; return false; }
    return true;
}

std::map<std::wstring, std::wstring> ReadMarker(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::map<std::wstring, std::wstring> output;
    std::string line;
    while (std::getline(file, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        output[Utf8ToWide(line.substr(0, equals))] = Utf8ToWide(line.substr(equals + 1));
    }
    return output;
}

}  // namespace

struct NativeUpdateManager::Impl {
    std::wstring executableDirectory;
    std::wstring currentVersion;
    Callback callback;
    mutable std::mutex mutex;
    NativeUpdateInfo latest;
    std::filesystem::path preparedPackage;
    std::wstring preparedVersion;
    std::thread worker;
    std::atomic_bool cancel{false};
    std::atomic_bool busy{false};

    ~Impl() {
        cancel.store(true);
        if (worker.joinable()) worker.join();
    }

    std::filesystem::path PendingDirectory() const { return std::filesystem::path(executableDirectory) / L"updates" / L"pending"; }
    std::filesystem::path MarkerPath() const { return PendingDirectory() / L"pending.txt"; }

    void Notify(NativeUpdateSnapshot snapshot) {
        Callback current;
        {
            std::lock_guard lock(mutex);
            current = callback;
        }
        if (current) current(snapshot);
    }

    void JoinPrevious() {
        if (worker.joinable()) worker.join();
    }

    bool LoadPrepared(std::wstring& error) {
        const auto values = ReadMarker(MarkerPath());
        const auto versionIt = values.find(L"version");
        const auto packageIt = values.find(L"package");
        if (versionIt == values.end() || packageIt == values.end()) return true;
        const std::filesystem::path package(packageIt->second);
        if (!std::filesystem::exists(package)) {
            std::error_code ec; std::filesystem::remove(MarkerPath(), ec);
            return true;
        }
        if (NativeUpdateManager::CompareVersions(currentVersion, versionIt->second) >= 0) {
            std::error_code ec; std::filesystem::remove_all(PendingDirectory(), ec);
            return true;
        }
        preparedVersion = versionIt->second;
        preparedPackage = package;
        latest.version = preparedVersion;
        latest.source = values.count(L"source") ? values.at(L"source") : L"本地缓存";
        latest.sha256 = values.count(L"sha256") ? values.at(L"sha256") : L"";
        latest.available = true;
        return true;
    }

    bool DownloadFile(const NativeUpdateInfo& info, const std::filesystem::path& target,
                      std::wstring expectedSha, std::wstring& error) {
        InternetHandle session, connection, request;
        if (!OpenRequest(info.downloadUrl, session, connection, request, error)) return false;
        DWORD contentLength = 0, size = sizeof(contentLength);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &size, WINHTTP_NO_HEADER_INDEX)) contentLength = 0;
        if (contentLength > kMaximumUpdateBytes) { error = L"更新包超过允许大小"; return false; }
        std::ofstream file(target, std::ios::binary | std::ios::trunc);
        if (!file) { error = L"无法创建更新包文件"; return false; }

        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        DWORD objectLength = 0, resultLength = 0;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
            BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) < 0) {
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
            error = L"无法初始化 SHA-256 校验"; return false;
        }
        std::vector<unsigned char> hashObject(objectLength);
        if (BCryptCreateHash(algorithm, &hash, hashObject.data(), objectLength, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0); error = L"无法创建 SHA-256 校验"; return false;
        }

        std::array<unsigned char, 65536> buffer{};
        std::uint64_t downloaded = 0;
        auto started = std::chrono::steady_clock::now();
        auto lastReport = started;
        std::uint64_t lastBytes = 0;
        bool success = true;
        while (!cancel.load()) {
            DWORD read = 0;
            if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
                error = L"下载更新包失败：" + ErrorText(GetLastError()); success = false; break;
            }
            if (!read) break;
            downloaded += read;
            if (downloaded > kMaximumUpdateBytes) { error = L"更新包超过允许大小"; success = false; break; }
            file.write(reinterpret_cast<const char*>(buffer.data()), read);
            if (!file || BCryptHashData(hash, buffer.data(), read, 0) < 0) { error = L"写入或校验更新包失败"; success = false; break; }
            const auto now = std::chrono::steady_clock::now();
            if (now - lastReport >= std::chrono::milliseconds(200)) {
                const double seconds = std::chrono::duration<double>(now - lastReport).count();
                const auto speed = seconds > 0 ? static_cast<std::uint64_t>((downloaded - lastBytes) / seconds) : 0;
                NativeUpdateSnapshot snapshot; snapshot.phase = NativeUpdatePhase::Downloading; snapshot.latest = info;
                snapshot.message = L"正在下载更新包"; snapshot.downloadedBytes = downloaded; snapshot.totalBytes = contentLength; snapshot.bytesPerSecond = speed;
                Notify(std::move(snapshot));
                lastReport = now; lastBytes = downloaded;
            }
        }
        file.close();
        if (cancel.load()) { error = L"下载已取消"; success = false; }
        std::array<unsigned char, 32> digest{};
        if (success && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) { error = L"完成 SHA-256 校验失败"; success = false; }
        BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0);
        if (!success) return false;
        NativeUpdateSnapshot verifying; verifying.phase = NativeUpdatePhase::Verifying; verifying.latest = info;
        verifying.message = L"正在校验更新包"; verifying.downloadedBytes = downloaded; verifying.totalBytes = contentLength;
        Notify(std::move(verifying));
        const std::wstring actual = HexDigest(digest);
        if (!expectedSha.empty() && !EqualsInsensitive(actual, expectedSha)) {
            error = L"更新包 SHA-256 校验失败"; return false;
        }
        return true;
    }
};

NativeUpdateManager::NativeUpdateManager() : impl_(std::make_unique<Impl>()) {}
NativeUpdateManager::~NativeUpdateManager() = default;

bool NativeUpdateManager::Initialize(const std::wstring& executableDirectory,
                                     const std::wstring& currentVersion,
                                     Callback callback,
                                     std::wstring& error) {
    impl_->executableDirectory = executableDirectory;
    impl_->currentVersion = currentVersion;
    impl_->callback = std::move(callback);
    std::error_code ec;
    std::filesystem::create_directories(impl_->PendingDirectory(), ec);
    if (ec) { error = L"无法创建更新缓存目录"; return false; }
    return impl_->LoadPrepared(error);
}

void NativeUpdateManager::Check(bool manual) {
    if (impl_->busy.exchange(true)) return;
    impl_->cancel.store(false);
    impl_->JoinPrevious();
    impl_->worker = std::thread([this, manual] {
        NativeUpdateSnapshot checking; checking.phase = NativeUpdatePhase::Checking; checking.manual = manual; checking.message = L"正在检查更新";
        impl_->Notify(checking);
        NativeUpdateInfo info; std::wstring error;
        const bool ok = CheckLatest(impl_->currentVersion, info, error);
        NativeUpdateSnapshot snapshot; snapshot.manual = manual;
        if (!ok) {
            snapshot.phase = NativeUpdatePhase::Error; snapshot.message = L"检查更新失败"; snapshot.error = error;
        } else {
            {
                std::lock_guard lock(impl_->mutex);
                impl_->latest = info;
            }
            snapshot.latest = info;
            snapshot.phase = info.available ? NativeUpdatePhase::Available : NativeUpdatePhase::UpToDate;
            snapshot.message = info.available ? L"发现新版本" : L"当前已是最新版本";
        }
        impl_->busy.store(false);
        impl_->Notify(std::move(snapshot));
    });
}

void NativeUpdateManager::DownloadLatest() {
    if (impl_->busy.exchange(true)) return;
    impl_->cancel.store(false);
    impl_->JoinPrevious();
    NativeUpdateInfo info;
    {
        std::lock_guard lock(impl_->mutex);
        info = impl_->latest;
    }
    impl_->worker = std::thread([this, info] {
        NativeUpdateSnapshot preparing; preparing.phase = NativeUpdatePhase::Preparing; preparing.latest = info; preparing.message = L"正在准备更新包";
        impl_->Notify(preparing);
        std::wstring error;
        std::wstring expectedSha = info.sha256;
        if (expectedSha.empty() && !info.checksumUrl.empty()) {
            if (!FetchChecksum(info.checksumUrl, expectedSha, error)) {
                NativeUpdateSnapshot failed; failed.phase = NativeUpdatePhase::Error; failed.latest = info; failed.message = L"读取校验文件失败"; failed.error = error;
                impl_->busy.store(false); impl_->Notify(std::move(failed)); return;
            }
        }
        std::error_code ec;
        std::filesystem::remove_all(impl_->PendingDirectory(), ec);
        std::filesystem::create_directories(impl_->PendingDirectory(), ec);
        if (ec) {
            NativeUpdateSnapshot failed; failed.phase = NativeUpdatePhase::Error; failed.latest = info; failed.message = L"准备更新目录失败"; failed.error = Utf8ToWide(ec.message());
            impl_->busy.store(false); impl_->Notify(std::move(failed)); return;
        }
        const auto package = impl_->PendingDirectory() / (L"WuwaEchoCalculator-v" + info.version + L"-windows-x64.zip");
        if (!impl_->DownloadFile(info, package, expectedSha, error)) {
            std::filesystem::remove(package, ec);
            NativeUpdateSnapshot failed; failed.phase = impl_->cancel.load() ? NativeUpdatePhase::Cancelled : NativeUpdatePhase::Error;
            failed.latest = info; failed.message = impl_->cancel.load() ? L"下载已取消" : L"更新包准备失败"; failed.error = error;
            impl_->busy.store(false); impl_->Notify(std::move(failed)); return;
        }
        NativeUpdateInfo markerInfo = info; markerInfo.sha256 = expectedSha;
        if (!WriteMarker(impl_->MarkerPath(), markerInfo, package, error)) {
            NativeUpdateSnapshot failed; failed.phase = NativeUpdatePhase::Error; failed.latest = info; failed.message = L"保存待安装更新失败"; failed.error = error;
            impl_->busy.store(false); impl_->Notify(std::move(failed)); return;
        }
        {
            std::lock_guard lock(impl_->mutex);
            impl_->preparedPackage = package;
            impl_->preparedVersion = info.version;
            impl_->latest = markerInfo;
        }
        NativeUpdateSnapshot ready; ready.phase = NativeUpdatePhase::Ready; ready.latest = markerInfo; ready.message = L"更新包已准备完成"; ready.packagePath = package.wstring();
        impl_->busy.store(false); impl_->Notify(std::move(ready));
    });
}

void NativeUpdateManager::CancelDownload() { impl_->cancel.store(true); }

bool NativeUpdateManager::HasPreparedUpdate() const {
    std::lock_guard lock(impl_->mutex);
    return !impl_->preparedVersion.empty() && !impl_->preparedPackage.empty() && std::filesystem::exists(impl_->preparedPackage);
}

std::wstring NativeUpdateManager::PreparedVersion() const {
    std::lock_guard lock(impl_->mutex); return impl_->preparedVersion;
}

NativeUpdateInfo NativeUpdateManager::LatestInfo() const {
    std::lock_guard lock(impl_->mutex); return impl_->latest;
}

bool NativeUpdateManager::LaunchPreparedInstaller(std::wstring& error) {
    std::filesystem::path package;
    std::wstring version;
    {
        std::lock_guard lock(impl_->mutex);
        package = impl_->preparedPackage; version = impl_->preparedVersion;
    }
    if (package.empty() || !std::filesystem::exists(package)) { error = L"尚未准备更新包"; return false; }
    const auto helper = std::filesystem::path(impl_->executableDirectory) / L"WuwaEchoUpdater.exe";
    if (!std::filesystem::exists(helper)) { error = L"缺少更新助手 WuwaEchoUpdater.exe"; return false; }
    const auto runHelper = impl_->PendingDirectory() / L"WuwaEchoUpdater.run.exe";
    std::error_code ec;
    std::filesystem::copy_file(helper, runHelper, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) { error = L"准备更新助手失败：" + Utf8ToWide(ec.message()); return false; }
    const auto restart = std::filesystem::path(impl_->executableDirectory) / L"鸣潮声骸计算器.exe";
    std::wstring command = QuoteArgument(runHelper.wstring()) + L" --parent " + std::to_wstring(GetCurrentProcessId()) +
        L" --target " + QuoteArgument(impl_->executableDirectory) + L" --package " + QuoteArgument(package.wstring()) +
        L" --restart " + QuoteArgument(restart.wstring()) + L" --version " + QuoteArgument(version);
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end()); mutableCommand.push_back(L'\0');
    if (!CreateProcessW(runHelper.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
            impl_->PendingDirectory().c_str(), &startup, &process)) {
        error = L"启动更新助手失败：" + ErrorText(GetLastError()); return false;
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    NativeUpdateSnapshot snapshot; snapshot.phase = NativeUpdatePhase::LaunchingInstaller; snapshot.latest = LatestInfo(); snapshot.message = L"正在启动更新助手";
    impl_->Notify(std::move(snapshot));
    return true;
}

void NativeUpdateManager::ClearPreparedUpdate() {
    std::error_code ec;
    std::filesystem::remove_all(impl_->PendingDirectory(), ec);
    std::lock_guard lock(impl_->mutex);
    impl_->preparedPackage.clear(); impl_->preparedVersion.clear();
}

int NativeUpdateManager::CompareVersions(const std::wstring& left, const std::wstring& right) {
    std::array<int, 3> leftNumbers{}, rightNumbers{};
    std::wstring leftPre, rightPre;
    const bool leftOk = ParseVersion(left, leftNumbers, leftPre);
    const bool rightOk = ParseVersion(right, rightNumbers, rightPre);
    if (!leftOk || !rightOk) return _wcsicmp(left.c_str(), right.c_str());
    for (std::size_t index = 0; index < leftNumbers.size(); ++index) {
        if (leftNumbers[index] < rightNumbers[index]) return -1;
        if (leftNumbers[index] > rightNumbers[index]) return 1;
    }
    if (leftPre == rightPre) return 0;
    if (leftPre.empty()) return 1;
    if (rightPre.empty()) return -1;
    return _wcsicmp(leftPre.c_str(), rightPre.c_str());
}
