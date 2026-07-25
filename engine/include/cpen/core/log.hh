#ifndef CPEN_CORE_LOG_HH
#define CPEN_CORE_LOG_HH

#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <source_location>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

/// Compile-time severity floor, as the underlying value of `cpen::log::Level`.
/// Call sites below this floor expand to nothing: no formatting, no dispatch,
/// and — for side-effect-free arguments — no argument evaluation either.
/// Overridable per build via target_compile_definitions.
#ifndef CPEN_LOG_ACTIVE_LEVEL
#  ifdef NDEBUG
#    define CPEN_LOG_ACTIVE_LEVEL 2
#  else
#    define CPEN_LOG_ACTIVE_LEVEL 0
#  endif
#endif

/// Namespace is `cpen::log` rather than `cpen::core::log` even though the module
/// lives under core/: a `namespace log = ...` alias at global scope collides with
/// the C library's `::log`, and engine code inside `namespace cpen::X` reaches
/// `cpen::log` unqualified with no alias at all.
namespace cpen::log
{
    /// Severity. `OFF` is a threshold value only and must never appear in a Record.
    ///
    /// Note for Windows builds: `<wingdi.h>` defines a macro named ERROR. Any
    /// translation unit that includes <windows.h> alongside this header must
    /// define NOGDI (or #undef ERROR) first, as engine/src/core/log_sinks.cc does.
    enum class Level : std::uint8_t
    {
        TRACE,
        DEBUG,
        INFO,
        WARN,
        ERROR,
        CRITICAL,
        OFF,
    };

    /// Subsystem tag. Categories exist so one noisy subsystem can be silenced or
    /// raised independently of the rest; they mirror the engine layers.
    enum class Category : std::uint8_t
    {
        CORE,
        PLATFORM,
        RENDER,
        PRESENT,
        SCRIPT,
        VM,
        AUDIO,
        APP,
    };

    inline constexpr std::size_t CATEGORY_COUNT = 8;

    /// The compile-time cutoff as a `Level`. Comparing two enumerators keeps the
    /// check free of the integral-range tautology -Wtype-limits objects to when
    /// the cutoff sits at 0.
    inline constexpr Level ACTIVE_LEVEL = static_cast<Level>(CPEN_LOG_ACTIVE_LEVEL);

    /// True if calls at `level` survive the compile-time cutoff.
    constexpr bool compiled_in(const Level level) noexcept
    {
        return level >= ACTIVE_LEVEL;
    }

    /// Full lowercase name ("critical", "platform"). Sinks may abbreviate for
    /// display; this is the canonical spelling.
    std::string_view to_string(Level level) noexcept;
    std::string_view to_string(Category category) noexcept;

    /// One log event, fully assembled and handed to every sink.
    /// `message` is a non-owning view valid only for the duration of the sink
    /// call; a sink that stores records must copy it.
    struct Record
    {
        Level severity = Level::INFO;
        Category category = Category::CORE;
        std::chrono::system_clock::time_point time{};
        std::thread::id thread{};
        std::source_location location{};
        std::string_view message{};
    };

    /// Output destination. Formatting is a sink concern: the logger hands over a
    /// structured Record and each sink renders it as it sees fit. Asynchrony is
    /// likewise a sink concern — a future AsyncSink decorates any sink with a
    /// queue and a worker thread without touching a single call site.
    class Sink
    {
    public:
        virtual ~Sink() = default;

        Sink() = default;
        Sink(const Sink&) = delete;
        Sink& operator=(const Sink&) = delete;

        virtual void write(const Record& record) = 0;
        virtual void flush() {}

        void set_minimum_level(Level level) noexcept
        {
            this->threshold.store(level, std::memory_order_relaxed);
        }

        Level minimum_level() const noexcept
        {
            return this->threshold.load(std::memory_order_relaxed);
        }

    private:
        std::atomic<Level> threshold{Level::TRACE};
    };

    /// Fan-out front end. Deliberately a plain class with a global accessor
    /// rather than a singleton type: tests construct their own instances, and
    /// the global one is a convenience, not an enforced invariant.
    class Logger
    {
    public:
        void add_sink(std::shared_ptr<Sink> sink);
        void clear_sinks();

        /// Global floor. Applies to every category.
        void set_level(Level level) noexcept;

        /// Per-category floor. Only ever restricts further: the effective
        /// threshold is max(global, per-category), so a category cannot be made
        /// more verbose than the global floor allows.
        void set_level(Category category, Level level) noexcept;

        Level level_of(Category category) const noexcept;
        bool enabled(Level level, Category category) const noexcept;

        /// Fans `record` out to every sink that accepts its severity.
        /// With no sinks installed, falls back to stderr so that messages
        /// emitted before logging is configured are not lost.
        void write(const Record& record);
        void flush();

    private:
        mutable std::mutex mutex;
        std::vector<std::shared_ptr<Sink>> sinks;
        std::atomic<Level> global_level{Level::TRACE};
        std::array<std::atomic<Level>, CATEGORY_COUNT> category_levels{};
    };

    /// Process-wide logger used by the free functions below.
    Logger& default_logger() noexcept;

    /// Installs a ConsoleSink on the default logger and sets the global floor.
    /// Convenience for application start-up; not required — logging works
    /// (via the stderr fallback) even if this is never called.
    void initialize_console(Level minimum = Level::TRACE);

    /// Flushes every sink and terminates the process. Used for unrecoverable
    /// init failures, where there is no meaningful state to return to.
    [[noreturn]] void abort_now();

    namespace detail
    {
        /// Carries a compile-time-checked format string together with the call
        /// site of the *caller*. Default arguments are evaluated at the call
        /// site, so `source_location::current()` here yields the user's file and
        /// line, not this header's — which is what lets the whole API stay
        /// macro-free while still reporting useful locations.
        template <typename... Args>
        struct FormatWithLocation
        {
            template <typename StringLike>
                requires std::convertible_to<const StringLike&, std::string_view>
            consteval FormatWithLocation(
                const StringLike& text,
                std::source_location call_site = std::source_location::current())
                : format(text), location(call_site)
            {
            }

            std::format_string<Args...> format;
            std::source_location location;
        };

        /// Assembles the Record. Out-of-line so that <format> instantiations are
        /// the only template code the call site pays for.
        void dispatch(Level level, Category category,
                      const std::source_location& location, std::string_view message);

        template <typename... Args>
        void emit(Level level, Category category, const std::source_location& location,
                  std::format_string<Args...> format, Args&&... args)
        {
            if (!default_logger().enabled(level, category))
            {
                return;
            }
            dispatch(level, category, location, std::format(format, std::forward<Args>(args)...));
        }
    }

    // The `type_identity_t` wrapper puts `FormatWithLocation` in a non-deduced
    // context, so `Args` is deduced solely from the trailing pack — the same
    // technique std::format itself uses. Without it the format string would
    // participate in deduction and the pack would never match.

    template <typename... Args>
    void trace(Category category,
               detail::FormatWithLocation<std::type_identity_t<Args>...> message,
               Args&&... args)
    {
        if constexpr (compiled_in(Level::TRACE))
        {
            detail::emit(Level::TRACE, category, message.location, message.format,
                         std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void debug(Category category,
               detail::FormatWithLocation<std::type_identity_t<Args>...> message,
               Args&&... args)
    {
        if constexpr (compiled_in(Level::DEBUG))
        {
            detail::emit(Level::DEBUG, category, message.location, message.format,
                         std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void info(Category category,
              detail::FormatWithLocation<std::type_identity_t<Args>...> message,
              Args&&... args)
    {
        if constexpr (compiled_in(Level::INFO))
        {
            detail::emit(Level::INFO, category, message.location, message.format,
                         std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void warn(Category category,
              detail::FormatWithLocation<std::type_identity_t<Args>...> message,
              Args&&... args)
    {
        if constexpr (compiled_in(Level::WARN))
        {
            detail::emit(Level::WARN, category, message.location, message.format,
                         std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void error(Category category,
               detail::FormatWithLocation<std::type_identity_t<Args>...> message,
               Args&&... args)
    {
        if constexpr (compiled_in(Level::ERROR))
        {
            detail::emit(Level::ERROR, category, message.location, message.format,
                         std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void critical(Category category,
                  detail::FormatWithLocation<std::type_identity_t<Args>...> message,
                  Args&&... args)
    {
        if constexpr (compiled_in(Level::CRITICAL))
        {
            detail::emit(Level::CRITICAL, category, message.location, message.format,
                         std::forward<Args>(args)...);
        }
    }

    /// Logs at CRITICAL, flushes, and aborts. Never filtered by any threshold:
    /// the message describing why the process is dying must always get out.
    template <typename... Args>
    [[noreturn]] void fatal(Category category,
                            detail::FormatWithLocation<std::type_identity_t<Args>...> message,
                            Args&&... args)
    {
        detail::dispatch(Level::CRITICAL, category, message.location,
                         std::format(message.format, std::forward<Args>(args)...));
        abort_now();
    }
}

#endif //CPEN_CORE_LOG_HH
