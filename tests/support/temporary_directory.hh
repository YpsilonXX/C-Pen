#ifndef CPEN_TESTS_SUPPORT_TEMPORARY_DIRECTORY_HH
#define CPEN_TESTS_SUPPORT_TEMPORARY_DIRECTORY_HH

#include "cpen/core/file_system.hh"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <random>
#include <span>
#include <string_view>
#include <system_error>

namespace cpen::test
{
    /// A directory of its own, under the system temporary location, deleted with
    /// the object.
    ///
    /// The file layer and everything above it — the virtual file system, the
    /// resolver, the asset manager — are about real files, so their tests need
    /// real files. Fixtures committed to the repository would answer that, but
    /// they make the interesting cases (a file that is not there, a name in
    /// Cyrillic, a path that climbs out of its root) either impossible or ugly to
    /// express, and they cannot be made to differ per case.
    ///
    /// The name carries a per-process random value as well as a counter, so two
    /// suites running at once — the ordinary one and the GPU one, or two
    /// checkouts on a build machine — cannot collide.
    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            static const unsigned long long SEED = std::random_device{}();
            static std::atomic<unsigned> counter{0};

            this->root = std::filesystem::temp_directory_path() /
                std::format("cpen-tests-{:016x}-{}", SEED, counter++);

            std::filesystem::create_directories(this->root);
        }

        ~TemporaryDirectory()
        {
            // A destructor may not throw, and a leftover directory under /tmp is
            // not worth failing a run over: the error is swallowed on purpose.
            std::error_code error;
            std::filesystem::remove_all(this->root, error);
        }

        TemporaryDirectory(const TemporaryDirectory&) = delete;
        TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

        const std::filesystem::path& path() const noexcept { return this->root; }

        /// Full path of `relative` inside the directory, whether or not it exists.
        ///
        /// `relative` is UTF-8 and is converted as the engine converts, not
        /// through the narrow path constructor: on Windows the latter reads the
        /// bytes in the active code page, so a case that writes a file under a
        /// Cyrillic name and then reads it back through the engine would be
        /// comparing two different paths and would fail for that reason alone.
        std::filesystem::path resolve(const std::string_view relative) const
        {
            return this->root / core::path_from_utf8(relative);
        }

        /// Writes a file, creating whatever directories it sits in, and answers
        /// with its full path. Content is written byte for byte: no line ending is
        /// translated, which is what lets a case put a CRLF in a file and assert
        /// that reading it back gives an LF.
        std::filesystem::path write(const std::string_view relative,
                                    const std::string_view content) const
        {
            const std::filesystem::path file = this->resolve(relative);

            std::filesystem::create_directories(file.parent_path());

            std::ofstream stream(file, std::ios::binary | std::ios::trunc);
            stream.write(content.data(), static_cast<std::streamsize>(content.size()));

            return file;
        }

        std::filesystem::path write(const std::string_view relative,
                                    const std::span<const std::byte> content) const
        {
            return this->write(relative, std::string_view{
                reinterpret_cast<const char*>(content.data()), content.size()});
        }

    private:
        std::filesystem::path root;
    };
}

#endif //CPEN_TESTS_SUPPORT_TEMPORARY_DIRECTORY_HH
