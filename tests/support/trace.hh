#ifndef CPEN_TESTS_SUPPORT_TRACE_HH
#define CPEN_TESTS_SUPPORT_TRACE_HH

#include <format>
#include <iostream>
#include <string_view>
#include <utility>

namespace cpen::test
{
    /// Prints one step of a test case to stdout.
    ///
    /// Catch2 reports assertion counts, not intent: a passing run says nothing
    /// about what was actually exercised. These lines, together with the case
    /// banner printed by the event listener in trace_listener.cc, make a run
    /// readable end to end.
    ///
    /// Visible when the test binary is run directly, or through
    /// `ctest --output-on-failure` / `ctest -V`.
    template <typename... Args>
    void trace(const std::format_string<Args...> format, Args&&... args)
    {
        std::cout << "    - " << std::format(format, std::forward<Args>(args)...) << '\n';
    }

    /// Section heading inside a longer case.
    inline void trace_step(const std::string_view description)
    {
        std::cout << "  * " << description << '\n';
    }
}

#endif //CPEN_TESTS_SUPPORT_TRACE_HH
