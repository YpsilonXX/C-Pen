#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <iostream>
#include <string>

namespace
{
    /// Prints a banner around every test case so that a successful run still
    /// documents what was exercised, instead of collapsing into a single
    /// "All tests passed" line. Step details come from cpen::test::trace().
    class TraceListener final : public Catch::EventListenerBase
    {
    public:
        using Catch::EventListenerBase::EventListenerBase;

        void testCaseStarting(const Catch::TestCaseInfo& info) override
        {
            this->current_case = info.name;
            std::cout << "\n[CASE] " << info.name << "  " << info.tagsAsString() << '\n';
        }

        void sectionStarting(const Catch::SectionInfo& info) override
        {
            // Catch2 opens an implicit section named after the test case itself;
            // reporting it again would only duplicate the banner.
            if (info.name != this->current_case)
            {
                std::cout << "  * " << info.name << '\n';
            }
        }

        void testCaseEnded(const Catch::TestCaseStats& stats) override
        {
            const auto& assertions = stats.totals.assertions;
            std::cout << "[ END] " << assertions.passed << " passed, " << assertions.failed
                      << " failed\n";
        }

    private:
        std::string current_case;
    };
}

CATCH_REGISTER_LISTENER(TraceListener)
