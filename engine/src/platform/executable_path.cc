#include "cpen/platform/executable_path.hh"

#include "cpen/core/file_system.hh"

#include <system_error>

#ifdef _WIN32
    #include <vector>

    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif

    #include <windows.h>
#endif

namespace cpen::platform
{
    std::expected<std::filesystem::path, core::Error> executable_path()
    {
#ifdef _WIN32
        // The call gives no way to ask how long the answer is, and truncates
        // silently when the buffer is too small — reported only by the return
        // value being exactly the buffer size. Growing until it fits is the
        // documented way to get a path longer than MAX_PATH, which a Windows
        // install directory can be.
        std::vector<wchar_t> buffer(512);

        while (true)
        {
            const DWORD written = GetModuleFileNameW(nullptr, buffer.data(),
                                                     static_cast<DWORD>(buffer.size()));

            if (written == 0)
            {
                return std::unexpected(core::make_error(
                    core::ErrorCode::UNSPECIFIED,
                    "the path of the running executable could not be read (error {})",
                    GetLastError()));
            }

            if (written < buffer.size())
            {
                const std::filesystem::path path{
                    std::wstring{buffer.data(), static_cast<std::size_t>(written)}};

                std::error_code error;
                std::filesystem::path resolved = std::filesystem::canonical(path, error);

                return error ? path : resolved;
            }

            buffer.resize(buffer.size() * 2);
        }
#else
        // read_symlink rather than readlink(2): the standard library already
        // handles the buffer growth this needs, and /proc/self/exe answers with
        // the real image even for a process started through a symbolic link or
        // renamed while running.
        std::error_code error;
        std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);

        if (error)
        {
            return std::unexpected(core::make_error(
                core::ErrorCode::UNSPECIFIED,
                "the path of the running executable could not be read from "
                "/proc/self/exe: {}", error.message()));
        }

        std::filesystem::path resolved = std::filesystem::canonical(path, error);

        return error ? path : resolved;
#endif
    }

    std::expected<std::filesystem::path, core::Error> executable_directory()
    {
        return executable_path().transform([](const std::filesystem::path& path)
        {
            return path.parent_path();
        });
    }
}
