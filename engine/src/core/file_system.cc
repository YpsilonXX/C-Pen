#include "cpen/core/file_system.hh"

#include <cstdint>
#include <fstream>
#include <ios>
#include <system_error>
#include <utility>

namespace cpen::core
{
    std::expected<std::vector<std::byte>, Error> read_file_bytes(
        const std::filesystem::path& path)
    {
        std::error_code system_error;
        const std::filesystem::file_status status = std::filesystem::status(path, system_error);

        if (!std::filesystem::exists(status))
        {
            return std::unexpected(make_error(ErrorCode::FILE_NOT_FOUND,
                                              "no file at '{}'", path_to_utf8(path)));
        }

        if (!std::filesystem::is_regular_file(status))
        {
            return std::unexpected(make_error(ErrorCode::UNSPECIFIED,
                                              "'{}' is not a regular file",
                                              path_to_utf8(path)));
        }

        const std::uintmax_t size = std::filesystem::file_size(path, system_error);

        if (system_error)
        {
            return std::unexpected(make_error(ErrorCode::UNSPECIFIED,
                                              "cannot size '{}': {}",
                                              path_to_utf8(path), system_error.message()));
        }

        std::ifstream stream(path, std::ios::binary);

        if (!stream)
        {
            return std::unexpected(make_error(ErrorCode::UNSPECIFIED,
                                              "cannot open '{}' for reading",
                                              path_to_utf8(path)));
        }

        std::vector<std::byte> content(static_cast<std::size_t>(size));

        if (size > 0)
        {
            // The size is the filesystem's answer from a moment ago, so it is a
            // hint rather than a promise: a file being written while it is read
            // yields fewer bytes than were announced. What arrived is kept and the
            // buffer is trimmed to it; only a genuine device error is a failure.
            stream.read(reinterpret_cast<char*>(content.data()),
                        static_cast<std::streamsize>(size));

            if (stream.bad())
            {
                return std::unexpected(make_error(ErrorCode::UNSPECIFIED,
                                                  "read of '{}' failed",
                                                  path_to_utf8(path)));
            }

            content.resize(static_cast<std::size_t>(stream.gcount()));
        }

        return content;
    }

    std::expected<std::string, Error> read_file_text(const std::filesystem::path& path)
    {
        return read_file_bytes(path).transform([](const std::vector<std::byte>& content)
        {
            return normalize_text(std::string{
                reinterpret_cast<const char*>(content.data()), content.size()});
        });
    }

    std::string normalize_text(std::string text)
    {
        constexpr std::string_view BYTE_ORDER_MARK = "\xEF\xBB\xBF";

        if (text.starts_with(BYTE_ORDER_MARK))
        {
            text.erase(0, BYTE_ORDER_MARK.size());
        }

        // One pass in place: every byte is kept except the CR of a CRLF pair, so
        // the write position only ever trails the read position.
        std::size_t write_offset = 0;

        for (std::size_t read_offset = 0; read_offset < text.size(); ++read_offset)
        {
            const bool is_carriage_return_of_pair =
                text[read_offset] == '\r' &&
                read_offset + 1 < text.size() &&
                text[read_offset + 1] == '\n';

            if (is_carriage_return_of_pair)
            {
                continue;
            }

            text[write_offset++] = text[read_offset];
        }

        text.resize(write_offset);
        return text;
    }

    std::filesystem::path path_from_utf8(const std::string_view text)
    {
        // char8_t is what std::filesystem::path recognises as "these bytes are
        // UTF-8"; the same bytes as a char sequence mean "these bytes are in
        // whatever the platform's narrow encoding happens to be".
        const std::u8string_view utf8{
            reinterpret_cast<const char8_t*>(text.data()), text.size()};

        return std::filesystem::path{utf8};
    }

    std::string path_to_utf8(const std::filesystem::path& path)
    {
        const std::u8string text = path.u8string();
        return std::string{reinterpret_cast<const char*>(text.data()), text.size()};
    }
}
