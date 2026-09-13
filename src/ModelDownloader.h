#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace usersync {

struct DownloadState {
    enum class Phase { Idle, Connecting, Downloading, Done, Error, Cancelled };

    Phase       phase = Phase::Idle;
    std::string filename;          // basename being downloaded
    std::string url;
    std::string out_path;
    uint64_t    bytes_received = 0;
    uint64_t    bytes_total    = 0;  // 0 if Content-Length unknown
    double      elapsed_seconds = 0.0;
    std::string error;
};

// Background HTTP downloader. One download in flight at a time -- start()
// returns false if a download is already running. WinHTTP under the hood,
// follows redirects (HuggingFace -> S3 etc.), reports byte/total progress.
class ModelDownloader {
public:
    ModelDownloader();
    ~ModelDownloader();

    ModelDownloader(const ModelDownloader&) = delete;
    ModelDownloader& operator=(const ModelDownloader&) = delete;

    bool start(const std::string& url, const std::string& out_path);
    void cancel();
    bool busy() const { return busy_.load(); }

    DownloadState snapshot() const;

private:
    void worker(std::string url, std::string out_path);

    mutable std::mutex mu_;
    DownloadState      state_;
    std::atomic<bool>  cancel_{false};
    std::atomic<bool>  busy_{false};
    std::thread        worker_;
};

}  // namespace usersync
