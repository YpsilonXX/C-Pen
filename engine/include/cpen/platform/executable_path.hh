#ifndef CPEN_PLATFORM_EXECUTABLE_PATH_HH
#define CPEN_PLATFORM_EXECUTABLE_PATH_HH

#include "cpen/core/error.hh"

#include <expected>
#include <filesystem>

namespace cpen::platform
{
    /// The full path of the running executable, as the operating system knows it.
    ///
    /// This is what every other path in the engine is measured from, and it is the
    /// only honest answer to "where is the game". The working directory is not:
    /// it is whatever the shell, the launcher, the desktop shortcut or the
    /// debugger happened to be in, and a game that reads its assets relative to it
    /// works when started one way and fails when started another. argv[0] is not
    /// either — it is whatever the parent process chose to put there, which for a
    /// program found on PATH is a bare name with no directory at all.
    ///
    /// Asked of the system rather than remembered, so it is correct however the
    /// process was started: /proc/self/exe on Linux, GetModuleFileNameW on
    /// Windows. Both give the real path of the image that is running.
    ///
    /// Symbolic links are resolved. A launcher script that links to the binary
    /// elsewhere would otherwise put the game's data directory somewhere the
    /// player never installed anything.
    std::expected<std::filesystem::path, core::Error> executable_path();

    /// The directory the executable is in. What the asset roots default to.
    std::expected<std::filesystem::path, core::Error> executable_directory();
}

#endif //CPEN_PLATFORM_EXECUTABLE_PATH_HH
