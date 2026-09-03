#include "hoppie/network.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace hoppie {
namespace {

using WinHttpHandle = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>;

std::wstring wide(std::string_view value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
}

NetworkResult failure(const NetworkTask& task, const char* category) {
    NetworkResult result;
    result.id = task.id;
    result.purpose = task.purpose;
    result.errorCategory = category;
    return result;
}

}  // namespace

NetworkResult performPlatformHttps(const NetworkTask& task) {
    auto result = failure(task, "transport");
    const auto url = wide(task.url);
    if (url.empty()) return failure(task, "invalid_url");

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
        return failure(task, "https_required");

    WinHttpHandle session(WinHttpOpen(L"HoppieDatalinkXP/1.0",
                                      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0),
                          WinHttpCloseHandle);
    if (!session) return result;
    const int timeout = static_cast<int>(std::min(task.timeoutSeconds, 60u) * 1000u);
    WinHttpSetTimeouts(session.get(), timeout, timeout, timeout, timeout);

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    WinHttpHandle connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0),
                             WinHttpCloseHandle);
    if (!connection) return result;

    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength != 0)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const wchar_t* method = task.method == HttpMethod::Post ? L"POST" : L"GET";
    WinHttpHandle request(WinHttpOpenRequest(connection.get(), method, path.c_str(), nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE),
                          WinHttpCloseHandle);
    if (!request) return result;
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy,
                          sizeof(redirectPolicy)))
        return failure(task, "transport");

    const auto contentType = wide("Content-Type: " + task.contentType + "\r\n");
    LPVOID body = task.method == HttpMethod::Post
                      ? static_cast<void*>(const_cast<char*>(task.body.data()))
                      : WINHTTP_NO_REQUEST_DATA;
    const auto bodySize = task.method == HttpMethod::Post
                              ? static_cast<DWORD>(task.body.size())
                              : 0u;
    if (!WinHttpSendRequest(request.get(), contentType.c_str(),
                            static_cast<DWORD>(contentType.size()), body, bodySize, bodySize, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        const auto error = GetLastError();
        result.errorCategory = error == ERROR_WINHTTP_TIMEOUT ? "timeout" : "transport";
        return result;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    result.httpStatus = static_cast<int>(status);

    constexpr std::size_t maximumResponse = 16u * 1024u * 1024u;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) return result;
        if (available == 0) break;
        if (result.body.size() + available > maximumResponse)
            return failure(task, "response_too_large");
        std::vector<char> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), buffer.data(), available, &read)) return result;
        result.body.append(buffer.data(), read);
    }
    result.transportOk = true;
    result.errorCategory.clear();
    return result;
}

}  // namespace hoppie
