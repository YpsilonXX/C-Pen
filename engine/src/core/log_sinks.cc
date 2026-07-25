#include "cpen/core/log_sinks.hh"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string_view>

#ifdef _WIN32
#  include <io.h>
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
// <wingdi.h> defines a macro named ERROR, which would rewrite every
// `Level::ERROR` below into `Level::0`. NOGDI keeps it out; the #undef is a
// belt-and-braces guard in case some other Windows header reintroduces it.
#  define NOGDI
#  include <windows.h>
#  ifdef ERROR
#    undef ERROR
#  endif
#else
#  include <unistd.h>
#endif

namespace cpen::log
{
    namespace
    {
        /// Recorded once, on first use, from whichever thread reaches the logger
        /// first — in practice the main thread during start-up. Used only to
        /// decide whether a thread id is worth printing.
        std::thread::id main_thread_id()
        {
            static const std::thread::id identifier = std::this_thread::get_id();
            return identifier;
        }

        /// Display spelling of a severity, capped at five characters so the
        /// column stays narrow. Distinct from to_string(), which is canonical.
        std::string_view short_name(const Level level) noexcept
        {
            switch (level)
            {
            case Level::TRACE:    return "trace";
            case Level::DEBUG:    return "debug";
            case Level::INFO:     return "info";
            case Level::WARN:     return "warn";
            case Level::ERROR:    return "error";
            case Level::CRITICAL: return "crit";
            case Level::OFF:      return "off";
            }
            return "?";
        }

        /// Strips the directory part of a __FILE__-style path. Purely cosmetic:
        /// absolute build paths make log lines unreadable.
        std::string_view base_name(const char* path) noexcept
        {
            if (path == nullptr)
            {
                return "?";
            }
            const std::string_view full_path{path};
            const std::size_t separator = full_path.find_last_of("/\\");
            return separator == std::string_view::npos
                ? full_path
                : full_path.substr(separator + 1);
        }

        /// Local wall-clock time. Deliberately built on localtime_r/localtime_s
        /// rather than chrono's zoned_time: the tz database is still uneven
        /// across the toolchains this project targets (notably MinGW).
        std::tm to_local_time(const std::chrono::system_clock::time_point time_point)
        {
            const std::time_t seconds = std::chrono::system_clock::to_time_t(time_point);
            std::tm result{};
#ifdef _WIN32
            localtime_s(&result, &seconds);
#else
            localtime_r(&seconds, &result);
#endif
            return result;
        }

        /// SGR escape per severity. Only the severity column is coloured —
        /// colouring the message body makes long output hard to read.
        std::string_view color_for(const Level level) noexcept
        {
            switch (level)
            {
            case Level::TRACE:    return "\033[90m";
            case Level::DEBUG:    return "\033[36m";
            case Level::INFO:     return "\033[32m";
            case Level::WARN:     return "\033[33m";
            case Level::ERROR:    return "\033[31m";
            case Level::CRITICAL: return "\033[1;31m";
            case Level::OFF:      return "";
            }
            return "";
        }

        bool stream_is_terminal(std::FILE* stream) noexcept
        {
#ifdef _WIN32
            return _isatty(_fileno(stream)) != 0;
#else
            return isatty(fileno(stream)) != 0;
#endif
        }

        /// Windows consoles do not interpret ANSI escapes unless the mode is set
        /// explicitly. No-op elsewhere.
        void enable_virtual_terminal([[maybe_unused]] std::FILE* stream) noexcept
        {
#ifdef _WIN32
            const HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stream)));
            if (handle == INVALID_HANDLE_VALUE)
            {
                return;
            }
            DWORD mode = 0;
            if (GetConsoleMode(handle, &mode) == 0)
            {
                return;
            }
            SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
        }

        /// https://no-color.org — any non-empty value disables colour.
        bool no_color_requested() noexcept
        {
            const char* value = std::getenv("NO_COLOR");
            return value != nullptr && value[0] != '\0';
        }

        /// std::thread::id has a stream inserter everywhere, but a std::formatter
        /// only in the newest standard libraries; the stream keeps this portable.
        std::string thread_id_string(const std::thread::id identifier)
        {
            std::ostringstream out;
            out << identifier;
            return std::move(out).str();
        }
    }

    std::string format_record(const Record& record, const bool verbose, const bool color)
    {
        const std::tm local_time = to_local_time(record.time);
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            record.time.time_since_epoch()) % std::chrono::seconds{1};

        const std::string_view escape = color ? color_for(record.severity) : std::string_view{};
        const std::string_view reset = color ? std::string_view{"\033[0m"} : std::string_view{};

        std::string line = std::format(
            "{:02}:{:02}:{:02}.{:03}  {}{:<5}{}  {:<8}  {}:{}  {}",
            local_time.tm_hour, local_time.tm_min, local_time.tm_sec, milliseconds.count(),
            escape, short_name(record.severity), reset,
            to_string(record.category),
            base_name(record.location.file_name()),
            record.location.line(),
            record.message);

        if (verbose)
        {
            line += std::format("  (in {})", record.location.function_name());
        }

        if (record.thread != main_thread_id())
        {
            line += std::format("  [thread {}]", thread_id_string(record.thread));
        }

        return line;
    }

    ConsoleSink::ConsoleSink(std::FILE* output)
        : stream(output != nullptr ? output : stdout)
    {
        static_cast<void>(main_thread_id());

        this->color = stream_is_terminal(this->stream) && !no_color_requested();
        if (this->color)
        {
            enable_virtual_terminal(this->stream);
        }
    }

    void ConsoleSink::write(const Record& record)
    {
        const std::lock_guard lock(this->mutex);
        const std::string line = format_record(record, false, this->color);
        std::fputs(line.c_str(), this->stream);
        std::fputc('\n', this->stream);
    }

    void ConsoleSink::flush()
    {
        const std::lock_guard lock(this->mutex);
        std::fflush(this->stream);
    }

    FileSink::FileSink(const std::filesystem::path& path, const bool truncate_existing)
    {
        // std::filesystem::path handles the UTF-8/UTF-16 boundary on Windows;
        // this is the only place the log module touches the filesystem.
#ifdef _WIN32
        this->stream = _wfopen(path.c_str(), truncate_existing ? L"w" : L"a");
#else
        this->stream = std::fopen(path.c_str(), truncate_existing ? "w" : "a");
#endif
    }

    FileSink::~FileSink()
    {
        if (this->stream != nullptr)
        {
            std::fclose(this->stream);
        }
    }

    void FileSink::write(const Record& record)
    {
        if (this->stream == nullptr)
        {
            return;
        }
        const std::lock_guard lock(this->mutex);
        const std::string line = format_record(record, true, false);
        std::fputs(line.c_str(), this->stream);
        std::fputc('\n', this->stream);
    }

    void FileSink::flush()
    {
        if (this->stream == nullptr)
        {
            return;
        }
        const std::lock_guard lock(this->mutex);
        std::fflush(this->stream);
    }

    void CapturingSink::write(const Record& record)
    {
        const std::lock_guard lock(this->mutex);
        this->entries.push_back(Entry{
            .severity = record.severity,
            .category = record.category,
            .message = std::string{record.message},
            .file = std::string{base_name(record.location.file_name())},
            .line = record.location.line(),
        });
    }

    std::vector<CapturingSink::Entry> CapturingSink::snapshot() const
    {
        const std::lock_guard lock(this->mutex);
        return this->entries;
    }

    std::size_t CapturingSink::size() const
    {
        const std::lock_guard lock(this->mutex);
        return this->entries.size();
    }

    void CapturingSink::clear()
    {
        const std::lock_guard lock(this->mutex);
        this->entries.clear();
    }

    bool CapturingSink::contains(const std::string_view needle) const
    {
        const std::lock_guard lock(this->mutex);
        return std::ranges::any_of(this->entries, [needle](const Entry& entry)
        {
            return entry.message.find(needle) != std::string::npos;
        });
    }
}
