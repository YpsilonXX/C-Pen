#include <catch2/catch_test_macros.hpp>

#include "cpen/core/log.hh"
#include "cpen/core/log_sinks.hh"

#include <memory>
#include <string>

using namespace cpen;

namespace
{
    /// Redirects the default logger to a capturing sink for the duration of a
    /// test and restores a clean state afterwards. The default logger is
    /// process-wide, so tests must not leave sinks or thresholds behind.
    class CaptureGuard
    {
    public:
        CaptureGuard()
            : sink(std::make_shared<log::CapturingSink>())
        {
            reset_levels();
            log::default_logger().clear_sinks();
            log::default_logger().add_sink(this->sink);
        }

        ~CaptureGuard()
        {
            log::default_logger().clear_sinks();
            reset_levels();
        }

        CaptureGuard(const CaptureGuard&) = delete;
        CaptureGuard& operator=(const CaptureGuard&) = delete;

        std::shared_ptr<log::CapturingSink> sink;

    private:
        static void reset_levels()
        {
            log::default_logger().set_level(log::Level::TRACE);
            for (std::size_t i = 0; i < log::CATEGORY_COUNT; ++i)
            {
                log::default_logger().set_level(static_cast<log::Category>(i), log::Level::TRACE);
            }
        }
    };
}

TEST_CASE("logger routes formatted messages to its sinks", "[core][log]")
{
    const CaptureGuard guard;

    log::info(log::Category::SCRIPT, "compiled {} ops in {:.1f} ms", 42, 3.25);

    REQUIRE(guard.sink->size() == 1);

    const auto entries = guard.sink->snapshot();
    CHECK(entries[0].severity == log::Level::INFO);
    CHECK(entries[0].category == log::Category::SCRIPT);
    CHECK(entries[0].message == "compiled 42 ops in 3.2 ms");
    CHECK(entries[0].file == "log_test.cc");
    CHECK(entries[0].line != 0);
}

TEST_CASE("global level suppresses everything below it", "[core][log]")
{
    const CaptureGuard guard;
    log::default_logger().set_level(log::Level::WARN);

    log::debug(log::Category::CORE, "invisible");
    log::info(log::Category::CORE, "also invisible");
    log::warn(log::Category::CORE, "visible");
    log::error(log::Category::CORE, "visible too");

    CHECK(guard.sink->size() == 2);
    CHECK(guard.sink->contains("visible"));
    CHECK_FALSE(guard.sink->contains("invisible"));
}

TEST_CASE("per-category level restricts one subsystem only", "[core][log]")
{
    const CaptureGuard guard;
    log::default_logger().set_level(log::Category::RENDER, log::Level::ERROR);

    log::info(log::Category::RENDER, "noisy renderer");
    log::info(log::Category::VM, "quiet vm");
    log::error(log::Category::RENDER, "renderer broke");

    const auto entries = guard.sink->snapshot();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].message == "quiet vm");
    CHECK(entries[1].message == "renderer broke");
}

TEST_CASE("a category cannot be more verbose than the global floor", "[core][log]")
{
    const CaptureGuard guard;
    log::default_logger().set_level(log::Level::ERROR);
    log::default_logger().set_level(log::Category::VM, log::Level::TRACE);

    log::info(log::Category::VM, "still filtered");

    CHECK(guard.sink->size() == 0);
    CHECK(log::default_logger().level_of(log::Category::VM) == log::Level::ERROR);
}

TEST_CASE("sinks filter independently of the logger", "[core][log]")
{
    const CaptureGuard guard;
    const auto quiet = std::make_shared<log::CapturingSink>();
    quiet->set_minimum_level(log::Level::ERROR);
    log::default_logger().add_sink(quiet);

    log::info(log::Category::APP, "chatter");
    log::error(log::Category::APP, "problem");

    CHECK(guard.sink->size() == 2);
    CHECK(quiet->size() == 1);
    CHECK(quiet->contains("problem"));
}

TEST_CASE("record location points at the call site, not the log header", "[core][log]")
{
    const CaptureGuard guard;

    log::warn(log::Category::PLATFORM, "here");
    const unsigned expected_line = __LINE__ - 1;

    const auto entries = guard.sink->snapshot();
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].file == "log_test.cc");
    CHECK(entries[0].line == expected_line);
}

TEST_CASE("compile-time cutoff matches the configured active level", "[core][log]")
{
    STATIC_REQUIRE(log::compiled_in(log::Level::CRITICAL));
    STATIC_REQUIRE(log::compiled_in(log::Level::ERROR));
    STATIC_REQUIRE(log::compiled_in(log::Level::INFO));
}

TEST_CASE("format_record lays out fixed columns", "[core][log]")
{
    const std::string message = "hello";
    const log::Record record{
        .severity = log::Level::WARN,
        .category = log::Category::AUDIO,
        .time = std::chrono::system_clock::now(),
        .thread = std::this_thread::get_id(),
        .location = std::source_location::current(),
        .message = message,
    };

    const std::string line = log::format_record(record, false);

    // HH:MM:SS.mmm is 12 characters, then two spaces before the severity column.
    CHECK(line.substr(12, 2) == "  ");
    CHECK(line.find("warn ") != std::string::npos);
    CHECK(line.find("audio   ") != std::string::npos);
    CHECK(line.find("log_test.cc:") != std::string::npos);
    CHECK(line.ends_with("  hello"));
    CHECK(line.find('\033') == std::string::npos);
}

TEST_CASE("format_record colours only the severity column", "[core][log]")
{
    const std::string message = "boom";
    const log::Record record{
        .severity = log::Level::ERROR,
        .category = log::Category::VM,
        .time = std::chrono::system_clock::now(),
        .thread = std::this_thread::get_id(),
        .location = std::source_location::current(),
        .message = message,
    };

    const std::string line = log::format_record(record, false, true);

    CHECK(line.find("\033[31merror\033[0m") != std::string::npos);
    CHECK(line.ends_with("  boom"));
}
