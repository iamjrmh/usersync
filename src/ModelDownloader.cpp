#include "ModelDownloader.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace usersync {

namespace {

#ifdef _WIN32
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(),
                                      nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
#endif

}  // namespace

ModelDownloader::ModelDownloader() = default;

ModelDownloader::~ModelDownloader() {
    cancel();
    if (worker_.joinable()) worker_.join();
}

void ModelDownloader::cancel() {
    if (busy_.load()) cancel_.store(true);
}

DownloadState ModelDownloader::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    return state_;
}

bool ModelDownloader::start(const std::string& url, const std::string& out_path) {
    if (busy_.load()) return false;
    if (worker_.joinable()) worker_.join();

    cancel_.store(false);
    busy_.store(true);
    {
        std::lock_guard<std::mutex> lk(mu_);
        state_                 = {};
        state_.phase           = DownloadState::Phase::Connecting;
        state_.url             = url;
        state_.out_path        = out_path;
        state_.filename        = std::filesystem::path(out_path).filename().string();
    }
    worker_ = std::thread(&ModelDownloader::worker, this, url, out_path);
    return true;
}

void ModelDownloader::worker(std::string url, std::string out_path) {
    const auto t_start = std::chrono::steady_clock::now();

    auto set_phase = [&](DownloadState::Phase ph) {
        std::lock_guard<std::mutex> lk(mu_);
        state_.phase = ph;
        state_.elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
    };
    auto set_error = [&](const std::string& msg) {
        std::lock_guard<std::mutex> lk(mu_);
        state_.phase = DownloadState::Phase::Error;
        state_.error = msg;
        state_.elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
    };

#ifndef _WIN32
    set_error("downloader only implemented on Windows");
    busy_.store(false);
    return;
#else

    // --- Parse URL ----------------------------------------------------------
    std::wstring wurl = widen(url);
    wchar_t host[256] = {0}, path_buf[2048] = {0}, scheme[16] = {0};
    URL_COMPONENTS urlc{};
    urlc.dwStructSize     = sizeof(urlc);
    urlc.lpszScheme       = scheme;   urlc.dwSchemeLength   = 16;
    urlc.lpszHostName     = host;     urlc.dwHostNameLength = 256;
    urlc.lpszUrlPath      = path_buf; urlc.dwUrlPathLength  = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &urlc)) {
        set_error("invalid URL: " + url);
        busy_.store(false);
        return;
    }
    const bool secure = (urlc.nScheme == INTERNET_SCHEME_HTTPS);

    // --- WinHTTP handles (RAII via cleanup lambda) --------------------------
    HINTERNET hSession = nullptr, hConnect = nullptr, hRequest = nullptr;
    FILE* fp = nullptr;
    auto cleanup = [&]() {
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
        if (fp) { std::fclose(fp); fp = nullptr; }
    };

    hSession = WinHttpOpen(L"usersync/1.0",
                           WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { set_error("WinHttpOpen failed"); cleanup(); busy_.store(false); return; }

    hConnect = WinHttpConnect(hSession, host, urlc.nPort, 0);
    if (!hConnect) { set_error("WinHttpConnect failed"); cleanup(); busy_.store(false); return; }

    hRequest = WinHttpOpenRequest(hConnect, L"GET", path_buf, nullptr,
                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  secure ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) { set_error("WinHttpOpenRequest failed"); cleanup(); busy_.store(false); return; }

    // Always follow redirects (HF -> S3).
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY,
                     &redirect_policy, sizeof(redirect_policy));

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        set_error("WinHttpSendRequest failed");
        cleanup(); busy_.store(false); return;
    }
    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        set_error("WinHttpReceiveResponse failed");
        cleanup(); busy_.store(false); return;
    }

    // HTTP status -- bail on 4xx/5xx instead of writing the error body to disk.
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                            WINHTTP_NO_HEADER_INDEX)) {
        if (status >= 400) {
            set_error("HTTP " + std::to_string(status) + " from server.\n"
                      "URL: " + url + "\n\n"
                      "404 usually means the model file isn't at that name in "
                      "the whisper.cpp HuggingFace repo. Check what's actually "
                      "there at:\n"
                      "  https://huggingface.co/ggerganov/whisper.cpp/tree/main");
            cleanup(); busy_.store(false); return;
        }
    }

    // Content-Length (may be absent on chunked transfers).
    uint64_t total = 0;
    wchar_t  len_str[64] = {0};
    DWORD    len_size    = sizeof(len_str);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
                            WINHTTP_HEADER_NAME_BY_INDEX, len_str, &len_size,
                            WINHTTP_NO_HEADER_INDEX)) {
        total = _wcstoui64(len_str, nullptr, 10);
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        state_.bytes_total = total;
    }

    // Make sure models/ exists.
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(out_path).parent_path(), ec);

    fp = std::fopen(out_path.c_str(), "wb");
    if (!fp) {
        set_error("could not open " + out_path + " for writing");
        cleanup(); busy_.store(false); return;
    }

    set_phase(DownloadState::Phase::Downloading);

    // --- Read loop ----------------------------------------------------------
    std::vector<char> buf(256 * 1024);
    uint64_t got = 0;
    while (true) {
        if (cancel_.load()) {
            cleanup();
            std::remove(out_path.c_str());
            {
                std::lock_guard<std::mutex> lk(mu_);
                state_.phase = DownloadState::Phase::Cancelled;
                state_.elapsed_seconds =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
            }
            busy_.store(false);
            return;
        }

        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail)) {
            set_error("WinHttpQueryDataAvailable failed");
            cleanup(); std::remove(out_path.c_str()); busy_.store(false); return;
        }
        if (avail == 0) break;  // end of stream

        const DWORD to_read = (DWORD)std::min<size_t>(buf.size(), avail);
        DWORD read_bytes = 0;
        if (!WinHttpReadData(hRequest, buf.data(), to_read, &read_bytes)) {
            set_error("WinHttpReadData failed");
            cleanup(); std::remove(out_path.c_str()); busy_.store(false); return;
        }
        if (read_bytes == 0) break;

        if (std::fwrite(buf.data(), 1, read_bytes, fp) != read_bytes) {
            set_error("disk write failed (out of space?)");
            cleanup(); std::remove(out_path.c_str()); busy_.store(false); return;
        }
        got += read_bytes;

        {
            std::lock_guard<std::mutex> lk(mu_);
            state_.bytes_received  = got;
            state_.elapsed_seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
        }
    }

    cleanup();

    {
        std::lock_guard<std::mutex> lk(mu_);
        state_.phase           = DownloadState::Phase::Done;
        state_.bytes_received  = got;
        state_.elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
    }
    busy_.store(false);
#endif
}

}  // namespace usersync
