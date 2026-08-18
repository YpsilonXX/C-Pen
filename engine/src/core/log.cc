#include "cpen/core/log.hh"

#include "cpen/core/log_sinks.hh"

#include <cstdio>
#include <cstdlib>

namespace cpen::log
{
    std::string_view to_string(const Level level) noexcept
    {
        switch (level)
        {
        case Level::TRACE:    return "trace";
        case Level::DEBUG:    return "debug";
        case Level::INFO:     return "info";
        case Level::WARN:     return "warn";
        case Level::ERROR:    return "error";
        case Level::CRITICAL: return "critical";
        case Level::OFF:      return "off";
        }
        return "?";
    }

    std::string_view to_string(const Category category) noexcept
    {
        switch (category)
        {
        case Category::CORE:     return "core";
        case Category::PLATFORM: return "platform";
        case Category::ASSETS:   return "assets";
        case Category::RENDER:   return "render";
        case Category::PRESENT:  return "present";
        case Category::SCRIPT:   return "script";
        case Category::VM:       return "vm";
        case Category::AUDIO:    return "audio";
        case Category::APP:      return "app";
        }
        return "?";
    }

    void Logger::add_sink(std::shared_ptr<Sink> sink)
    {
        if (sink == nullptr)
        {
            return;
        }
        const std::lock_guard lock(this->mutex);
        this->sinks.push_back(std::move(sink));
    }

    void Logger::clear_sinks()
    {
        const std::lock_guard lock(this->mutex);
        this->sinks.clear();
    }

    void Logger::set_level(const Level level) noexcept
    {
        this->global_level.store(level, std::memory_order_relaxed);
    }

    void Logger::set_level(const Category category, const Level level) noexcept
    {
        this->category_levels[static_cast<std::size_t>(category)]
            .store(level, std::memory_order_relaxed);
    }

    Level Logger::level_of(const Category category) const noexcept
    {
        const Level global = this->global_level.load(std::memory_order_relaxed);
        const Level local = this->category_levels[static_cast<std::size_t>(category)]
            .load(std::memory_order_relaxed);
        return local > global ? local : global;
    }

    bool Logger::enabled(const Level level, const Category category) const noexcept
    {
        // OFF is the highest enumerator, so no real severity ever passes it.
        return level >= this->level_of(category);
    }

    void Logger::write(const Record& record)
    {
        const std::lock_guard lock(this->mutex);

        if (this->sinks.empty())
        {
            // Pre-configuration fallback: a message emitted before any sink is
            // installed still reaches the developer rather than vanishing.
            const std::string line = format_record(record, false);
            std::fputs(line.c_str(), stderr);
            std::fputc('\n', stderr);
            return;
        }

        for (const std::shared_ptr<Sink>& sink : this->sinks)
        {
            if (record.severity >= sink->minimum_level())
            {
                sink->write(record);
            }
        }
    }

    void Logger::flush()
    {
        const std::lock_guard lock(this->mutex);
        for (const std::shared_ptr<Sink>& sink : this->sinks)
        {
            sink->flush();
        }
    }

    Logger& default_logger() noexcept
    {
        // Function-local static: constructed on first use, so logging works from
        // any static initialiser without depending on translation-unit order.
        static Logger instance;
        return instance;
    }

    void initialize_console(const Level minimum)
    {
        default_logger().set_level(minimum);
        default_logger().add_sink(std::make_shared<ConsoleSink>());
    }

    void abort_now()
    {
        default_logger().flush();
        std::fflush(nullptr);
        std::abort();
    }

    namespace detail
    {
        void dispatch(const Level level, const Category category,
                      const std::source_location& location, const std::string_view message)
        {
            const Record record{
                .severity = level,
                .category = category,
                .time = std::chrono::system_clock::now(),
                .thread = std::this_thread::get_id(),
                .location = location,
                .message = message,
            };
            default_logger().write(record);
        }
    }
}
