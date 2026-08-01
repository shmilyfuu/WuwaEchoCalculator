from pathlib import Path

SOURCE = Path('native/update_manager.cpp')
TARGET = Path('native/update_manager_fixed.cpp')
text = SOURCE.read_text(encoding='utf-8')


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one match, found {count}')
    text = text.replace(old, new, 1)


replace_once('#include <chrono>\n', '#include <chrono>\n#include <cwctype>\n', 'cwctype include')
replace_once(
    '''    URL_COMPONENTS components{};
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
    if (components.dwExtraInfoLength && components.lpszExtraInfo) requestPath.append(components.lpszExtraInfo, components.dwExtraInfoLength);''',
    '''    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        error = L"解析更新地址失败：" + ErrorText(GetLastError());
        return false;
    }
    if (!components.lpszHostName || !components.dwHostNameLength) {
        error = L"更新地址缺少服务器名称";
        return false;
    }
    const std::wstring hostName(components.lpszHostName, components.dwHostNameLength);
    std::wstring requestPath = components.lpszUrlPath && components.dwUrlPathLength
        ? std::wstring(components.lpszUrlPath, components.dwUrlPathLength) : L"/";
    if (components.dwExtraInfoLength && components.lpszExtraInfo)
        requestPath.append(components.lpszExtraInfo, components.dwExtraInfoLength);''',
    'URL component parsing',
)
replace_once(
    '    const std::wstring expected = L"WuwaEchoCalculator-v" + info.version + L"-windows-x64.zip";',
    '''    info.available = NativeUpdateManager::CompareVersions(currentVersion, info.version) < 0;
    if (!info.available) return true;
    const std::wstring expected = L"WuwaEchoCalculator-v" + info.version + L"-windows-x64.zip";''',
    'skip unavailable package lookup',
)
replace_once(
    '    info.available = NativeUpdateManager::CompareVersions(currentVersion, info.version) < 0;\n    return true;',
    '    return true;',
    'remove duplicate availability calculation',
)

replace_once(
    '''        std::wstring error;
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
        NativeUpdateInfo markerInfo = info; markerInfo.sha256 = expectedSha;''',
    '''        std::wstring error;
        NativeUpdateInfo selectedInfo = info;
        auto resolveChecksum = [&](const NativeUpdateInfo& candidate, std::wstring& checksum, std::wstring& checksumError) {
            checksum = candidate.sha256;
            if (checksum.empty() && !candidate.checksumUrl.empty())
                return FetchChecksum(candidate.checksumUrl, checksum, checksumError);
            return true;
        };
        std::wstring expectedSha;
        if (!resolveChecksum(selectedInfo, expectedSha, error)) {
            NativeUpdateSnapshot failed; failed.phase = NativeUpdatePhase::Error; failed.latest = selectedInfo; failed.message = L"读取校验文件失败"; failed.error = error;
            impl_->busy.store(false); impl_->Notify(std::move(failed)); return;
        }
        std::error_code ec;
        std::filesystem::remove_all(impl_->PendingDirectory(), ec);
        std::filesystem::create_directories(impl_->PendingDirectory(), ec);
        if (ec) {
            NativeUpdateSnapshot failed; failed.phase = NativeUpdatePhase::Error; failed.latest = selectedInfo; failed.message = L"准备更新目录失败"; failed.error = Utf8ToWide(ec.message());
            impl_->busy.store(false); impl_->Notify(std::move(failed)); return;
        }
        const auto package = impl_->PendingDirectory() / (L"WuwaEchoCalculator-v" + selectedInfo.version + L"-windows-x64.zip");
        bool downloaded = impl_->DownloadFile(selectedInfo, package, expectedSha, error);
        if (!downloaded && !impl_->cancel.load() && EqualsInsensitive(selectedInfo.source, L"Gitee")) {
            std::filesystem::remove(package, ec);
            NativeUpdateInfo fallback;
            std::wstring fallbackError;
            if (FetchRelease(L"GitHub", kGithubLatestUrl, kGithubPageBase, false, impl_->currentVersion, fallback, fallbackError) &&
                fallback.available && EqualsInsensitive(fallback.version, selectedInfo.version)) {
                std::wstring fallbackSha;
                if (resolveChecksum(fallback, fallbackSha, fallbackError)) {
                    NativeUpdateSnapshot switching; switching.phase = NativeUpdatePhase::Preparing; switching.latest = fallback;
                    switching.message = L"Gitee 下载失败，正在切换到 GitHub";
                    impl_->Notify(std::move(switching));
                    selectedInfo = fallback;
                    expectedSha = fallbackSha;
                    error.clear();
                    downloaded = impl_->DownloadFile(selectedInfo, package, expectedSha, error);
                }
            }
            if (!downloaded && !fallbackError.empty()) {
                if (!error.empty()) error += L"；";
                error += fallbackError;
            }
        }
        if (!downloaded) {
            std::filesystem::remove(package, ec);
            NativeUpdateSnapshot failed; failed.phase = impl_->cancel.load() ? NativeUpdatePhase::Cancelled : NativeUpdatePhase::Error;
            failed.latest = selectedInfo; failed.message = impl_->cancel.load() ? L"下载已取消" : L"更新包准备失败"; failed.error = error;
            impl_->busy.store(false); impl_->Notify(std::move(failed)); return;
        }
        NativeUpdateInfo markerInfo = selectedInfo; markerInfo.sha256 = expectedSha;''',
    'Gitee download fallback',
)
replace_once('            impl_->preparedVersion = info.version;', '            impl_->preparedVersion = markerInfo.version;', 'prepared fallback version')

TARGET.write_text(text, encoding='utf-8')
print(f'Generated {TARGET} ({len(text)} chars)')
