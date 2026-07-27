#ifndef CPEN_TESTS_SUPPORT_LOG_CAPTURE_HH
#define CPEN_TESTS_SUPPORT_LOG_CAPTURE_HH

#include "cpen/core/log.hh"
#include "cpen/core/log_sinks.hh"

#include <cstddef>
#include <memory>
#include <string_view>

namespace cpen::test
{
    /// Redirects the default logger into a capturing sink for the lifetime of the
    /// guard.
    ///
    /// Two purposes: asserting that a component reported a problem, and keeping
    /// deliberately provoked errors out of the test binary's stderr, where they
    /// would read as genuine failures.
    class LogCaptureGuard
    {
    public:
        LogCaptureGuard()
            : sink(std::make_shared<log::CapturingSink>())
        {
            log::default_logger().clear_sinks();
            log::default_logger().add_sink(this->sink);
        }

        ~LogCaptureGuard()
        {
            log::default_logger().clear_sinks();
        }

        LogCaptureGuard(const LogCaptureGuard&) = delete;
        LogCaptureGuard& operator=(const LogCaptureGuard&) = delete;

        std::size_t size() const { return this->sink->size(); }
        bool contains(const std::string_view text) const { return this->sink->contains(text); }

        std::shared_ptr<log::CapturingSink> sink;
    };
}

#endif //CPEN_TESTS_SUPPORT_LOG_CAPTURE_HH
