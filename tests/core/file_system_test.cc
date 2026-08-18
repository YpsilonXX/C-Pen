#include <catch2/catch_test_macros.hpp>

#include "cpen/core/error.hh"
#include "cpen/core/file_system.hh"
#include "support/temporary_directory.hh"
#include "support/trace.hh"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using cpen::core::ErrorCode;
using cpen::core::normalize_text;
using cpen::core::path_from_utf8;
using cpen::core::path_to_utf8;
using cpen::core::read_file_bytes;
using cpen::core::read_file_text;
using cpen::test::TemporaryDirectory;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    std::vector<std::byte> bytes_of(const std::vector<std::uint8_t>& values)
    {
        std::vector<std::byte> converted;
        converted.reserve(values.size());

        for (const std::uint8_t value : values)
        {
            converted.push_back(static_cast<std::byte>(value));
        }

        return converted;
    }
}

TEST_CASE("a file reads back byte for byte", "[core][file_system]")
{
    const TemporaryDirectory directory;

    // Values chosen for what they break: a NUL ends a C string, 0xFF is not
    // valid UTF-8, and 0x1A ends a file for a stream opened in text mode on
    // Windows.
    const std::vector<std::byte> written = bytes_of({0x00, 0xFF, 0x1A, 0x0D, 0x0A, 0x41});
    directory.write("asset.bin", written);

    const auto content = read_file_bytes(directory.resolve("asset.bin"));

    REQUIRE(content.has_value());
    trace("read {} bytes", content->size());

    CHECK(*content == written);
}

TEST_CASE("an empty file is content, not a failure", "[core][file_system]")
{
    const TemporaryDirectory directory;
    directory.write("empty.txt", std::string_view{});

    const auto content = read_file_bytes(directory.resolve("empty.txt"));

    REQUIRE(content.has_value());
    CHECK(content->empty());
}

TEST_CASE("a missing file reports FILE_NOT_FOUND", "[core][file_system]")
{
    const TemporaryDirectory directory;

    const auto content = read_file_bytes(directory.resolve("absent.png"));

    REQUIRE_FALSE(content.has_value());
    trace("{}", content.error());

    // The one distinction a caller acts on: a missing asset can be replaced with
    // a placeholder, an unreadable one cannot.
    CHECK(content.error().code == ErrorCode::FILE_NOT_FOUND);

    // The message has to name the path, or a failure at startup says only that
    // something somewhere was missing.
    CHECK(content.error().message.find("absent.png") != std::string::npos);
}

TEST_CASE("a directory is not a missing file", "[core][file_system]")
{
    const TemporaryDirectory directory;
    std::filesystem::create_directories(directory.resolve("sprites"));

    const auto content = read_file_bytes(directory.resolve("sprites"));

    REQUIRE_FALSE(content.has_value());
    trace("{}", content.error());

    CHECK(content.error().code == ErrorCode::UNSPECIFIED);
}

TEST_CASE("text is read normalised", "[core][file_system]")
{
    const TemporaryDirectory directory;
    directory.write("script.pen", "\xEF\xBB\xBFlabel start\r\n    say \"Hi\"\r\n");

    const auto text = read_file_text(directory.resolve("script.pen"));

    REQUIRE(text.has_value());
    trace("read {} characters", text->size());

    CHECK(*text == "label start\n    say \"Hi\"\n");
}

TEST_CASE("normalize_text strips a leading byte order mark only", "[core][file_system]")
{
    constexpr std::string_view MARK = "\xEF\xBB\xBF";

    trace_step("at the start it is an editor's marker");
    CHECK(normalize_text(std::string{MARK} + "text") == "text");

    trace_step("anywhere else it is a zero-width no-break space the author wrote");
    CHECK(normalize_text("text" + std::string{MARK}) == "text\xEF\xBB\xBF");

    trace_step("and one mark is stripped, not every mark");
    CHECK(normalize_text(std::string{MARK} + std::string{MARK}) == "\xEF\xBB\xBF");
}

TEST_CASE("normalize_text folds CRLF and keeps a lone CR", "[core][file_system]")
{
    trace_step("pairs collapse wherever they are, including at the very end");
    CHECK(normalize_text("a\r\nb\r\n") == "a\nb\n");

    trace_step("a CR with no LF after it is data, not a line ending");
    CHECK(normalize_text("a\rb") == "a\rb");
    CHECK(normalize_text("trailing\r") == "trailing\r");

    trace_step("LF alone passes through untouched");
    CHECK(normalize_text("a\nb\n") == "a\nb\n");

    trace_step("a run of CRs before one LF loses only the CR that pairs with it");
    CHECK(normalize_text("a\r\r\nb") == "a\r\nb");

    CHECK(normalize_text("").empty());
}

TEST_CASE("a UTF-8 path survives the operating system boundary", "[core][file_system]")
{
    constexpr std::string_view NAME = "спрайты/алиса.png";

    const std::filesystem::path path = path_from_utf8(NAME);
    trace("round trip: {}", path_to_utf8(path));

    CHECK(path_to_utf8(path) == NAME);

    trace_step("and the same bytes reach a real file");
    const TemporaryDirectory directory;
    directory.write(NAME, "pixels");

    const auto content = read_file_text(directory.path() / path);

    REQUIRE(content.has_value());
    CHECK(*content == "pixels");
}
