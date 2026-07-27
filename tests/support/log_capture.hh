#ifndef CPEN_TESTS_SUPPORT_LOG_CAPTURE_HH
#define CPEN_TESTS_SUPPORT_LOG_CAPTURE_HH

#include "cpen/core/log.hh"
#include "cpen/core/log_sinks.hh"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

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

        /// Number of captured records of exactly `severity`.
        ///
        /// Distinct from size() because a component that reports a problem often
        /// logs something routine in the same call — a diagnostic for the part that
        /// was refused, a debug line for the part that still worked. Asserting on
        /// the total would then be an assertion about the trace output as much as
        /// about the diagnostic, and would break the moment a trace line was added
        /// anywhere along the path.
        std::size_t count(const log::Level severity) const
        {
            const std::vector<log::CapturingSink::Entry> entries = this->sink->snapshot();
            return static_cast<std::size_t>(
                std::ranges::count(entries, severity, &log::CapturingSink::Entry::severity));
        }

        std::shared_ptr<log::CapturingSink> sink;
    };
}

#endif //CPEN_TESTS_SUPPORT_LOG_CAPTURE_HH
