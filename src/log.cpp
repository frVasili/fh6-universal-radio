#include "fh6/log.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

namespace fh6::log {
namespace {
struct Record {
    Level level;
    std::string message;
};

std::FILE* g_file = nullptr;
std::mutex g_mu;
std::mutex g_file_mu;
std::condition_variable g_cv;
std::deque<Record> g_queue;
std::thread g_thread;
bool g_stopping = false;

constexpr std::size_t kMaxQueuedRecords = 256;

constexpr std::string_view level_name(Level l) noexcept {
    switch (l) {
        case Level::trace: return "TRACE";
        case Level::info: return "INFO";
        case Level::warn: return "WARN";
        case Level::error: return "ERROR";
    }
    return "?";
}

void write_record(std::FILE* file, const Record& record) noexcept {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t   = system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    char ts[32];
    std::snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d.%03lld", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<long long>(ms));
    const auto lvl = level_name(record.level);
    std::fprintf(file, "%s %-5.*s %.*s\n", ts, static_cast<int>(lvl.size()), lvl.data(),
                 static_cast<int>(record.message.size()), record.message.data());
}

void run() noexcept {
    for (;;) {
        std::deque<Record> batch;
        {
            std::unique_lock lock{g_mu};
            g_cv.wait(lock, [] { return g_stopping || !g_queue.empty(); });
            if (g_stopping && g_queue.empty()) return;
            batch.swap(g_queue);
        }
        // The queue mutex is not held during formatting or file I/O. Emitters
        // can therefore remain nonblocking even if the filesystem stalls.
        std::lock_guard file_lock{g_file_mu};
        if (!g_file) continue;
        bool flush = false;
        for (const auto& record : batch) {
            write_record(g_file, record);
            flush = flush || record.level == Level::warn || record.level == Level::error;
        }
        if (flush) std::fflush(g_file);
    }
}
} // namespace

void init(const std::filesystem::path& log_file) noexcept {
    shutdown();
    std::lock_guard lock{g_mu};
    g_file = _wfopen(log_file.c_str(), L"a");
    if (!g_file) return;
    g_stopping = false;
    g_thread = std::thread{run};
}

void shutdown() noexcept {
    {
        std::lock_guard lock{g_mu};
        if (!g_file && !g_thread.joinable()) return;
        g_stopping = true;
    }
    g_cv.notify_one();
    if (g_thread.joinable()) g_thread.join();
    std::lock_guard lock{g_mu};
    if (g_file) {
        std::fflush(g_file);
        std::fclose(g_file);
        g_file = nullptr;
    }
    g_queue.clear();
    g_stopping = false;
}

void emit(Level level, std::string_view message) noexcept {
    std::lock_guard lock{g_mu};
    if (!g_file || g_stopping) return;

    if (g_queue.size() >= kMaxQueuedRecords) {
        // Informational chatter is expendable; preserve warnings/errors by
        // making room for them without ever waiting on the file from the
        // caller thread.
        if (level == Level::trace || level == Level::info) return;
        auto it = std::find_if(g_queue.begin(), g_queue.end(), [](const Record& r) {
            return r.level == Level::trace || r.level == Level::info;
        });
        if (it != g_queue.end()) g_queue.erase(it);
        else return; // bounded queue wins over blocking a timing-sensitive caller
    }
    g_queue.push_back(Record{level, std::string{message}});
    g_cv.notify_one();
}

} // namespace fh6::log
