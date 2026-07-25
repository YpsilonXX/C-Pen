#ifndef CPEN_CORE_LOG_SINKS_HH
#define CPEN_CORE_LOG_SINKS_HH

#include "cpen/core/log.hh"

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace cpen::log
{
    /// Renders a Record as a single line, in fixed columns:
    ///   HH:MM:SS.mmm  level  category  file:line  message
    /// The level and category columns are space-padded rather than bracketed:
    /// brackets turn the padding into visible empty boxes, which reads as if
    /// something is missing. CRITICAL is abbreviated to "crit" to keep the
    /// severity column five characters wide.
    ///
    /// `verbose` appends the originating function; `color` wraps the severity
    /// column in an SGR escape.
    std::string format_record(const Record& record, bool verbose, bool color = false);

    /// Writes to a C stdio stream. Defaults to stdout: IDE run consoles (CLion
    /// among them) tint the whole of stderr red, which makes ordinary info lines
    /// look like failures. Severity is conveyed by the ANSI colour instead, and
    /// keeping to one stream preserves line ordering under redirection.
    ///
    /// Colour is enabled only when the stream is a terminal and NO_COLOR is
    /// unset; on Windows the virtual-terminal console mode is enabled on demand.
    class ConsoleSink final : public Sink
    {
    public:
        explicit ConsoleSink(std::FILE* output = stdout);

        void write(const Record& record) override;
        void flush() override;

        void set_color(bool enabled) noexcept { this->color = enabled; }
        bool color_enabled() const noexcept { return this->color; }

    private:
        std::FILE* stream = nullptr;
        bool color = false;
        std::mutex mutex;
    };

    /// Appends to a file. Never coloured, always verbose — a file is read after
    /// the fact, when the extra context is what makes the line useful.
    class FileSink final : public Sink
    {
    public:
        explicit FileSink(const std::filesystem::path& path, bool truncate_existing = true);
        ~FileSink() override;

        void write(const Record& record) override;
        void flush() override;

        bool is_open() const noexcept { return this->stream != nullptr; }

    private:
        std::FILE* stream = nullptr;
        std::mutex mutex;
    };

    /// Test sink: keeps records in memory so a test can assert on what the code
    /// under test logged. Owns copies of the message and location strings, since
    /// a Record's views die with the dispatch call.
    class CapturingSink final : public Sink
    {
    public:
        struct Entry
        {
            Level severity = Level::INFO;
            Category category = Category::CORE;
            std::string message;
            std::string file;
            std::uint_least32_t line = 0;
        };

        void write(const Record& record) override;

        std::vector<Entry> snapshot() const;
        std::size_t size() const;
        void clear();

        /// True if any captured message contains `needle`.
        bool contains(std::string_view needle) const;

    private:
        mutable std::mutex mutex;
        std::vector<Entry> entries;
    };
}

#endif //CPEN_CORE_LOG_SINKS_HH
