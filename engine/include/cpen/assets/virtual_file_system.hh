#ifndef CPEN_ASSETS_VIRTUAL_FILE_SYSTEM_HH
#define CPEN_ASSETS_VIRTUAL_FILE_SYSTEM_HH

#include "cpen/assets/virtual_path.hh"
#include "cpen/core/error.hh"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace cpen::assets
{
    /// Whether the case audit runs unless a caller says otherwise.
    ///
    /// On in a debug build, off in a release one. The audit costs a directory
    /// scan per path component and answers a question about how the game was
    /// authored, not about how it is running: the person who can act on it is the
    /// one building the game, and they are the one running the debug build.
    inline constexpr bool DEFAULT_PATH_AUDIT =
#ifdef NDEBUG
        false;
#else
        true;
#endif

    /// A case mismatch, kept for the summary printed when the game exits.
    struct CaseMismatchRecord
    {
        std::string virtual_path;
        std::filesystem::path root;
        std::vector<PathCaseMismatch> segments;

        /// Whether the asset loaded despite the mismatch. Both outcomes are the
        /// same authoring mistake; which one is seen depends on the file system
        /// the game happens to be running on.
        CaseMismatchOutcome outcome = CaseMismatchOutcome::LOADED_ANYWAY;
    };

    /// Read-only access to the game's files through virtual paths.
    ///
    /// Everything above this class asks for "fonts/ui.ttf" and never learns where
    /// that came from — which directory, or eventually which archive. That is the
    /// whole point of the class, and the reason it is concrete rather than an
    /// interface with one implementation: the seam that lets a .pak backend land
    /// later is the vocabulary its callers speak, not a virtual function they
    /// call through. When the archive arrives it changes what is inside these
    /// methods and nothing above them.
    ///
    /// Roots are searched in the order they were mounted, and the first hit wins.
    /// The Application mounts the game's directory first and the engine's second,
    /// so a game that ships its own "fonts/ui.ttf" replaces the engine's without
    /// having to know that there was one.
    ///
    /// Single-threaded, like the rest of the core loop. The audit bookkeeping is
    /// mutable and unsynchronised; when asset loading moves to a worker thread,
    /// this is one of the places that has to be revisited.
    class VirtualFileSystem
    {
    public:
        /// Adds a directory to the end of the search order.
        ///
        /// A root that does not exist is kept and complained about rather than
        /// refused: the mistake is worth a line in the log, and the game is
        /// better off starting and reporting every missing asset than dying at
        /// the mount with no idea which files it was going to need.
        void mount(std::filesystem::path root);

        std::span<const std::filesystem::path> roots() const noexcept
        {
            return this->mounted_roots;
        }

        /// The real path of `virtual_path`, from the first root that has it.
        ///
        /// Fails with FILE_NOT_FOUND when no root does — with a message naming
        /// every root that was searched, because "asset not found" without that
        /// list is the least actionable diagnostic in a game engine.
        ///
        /// When nothing is found, the case audit runs whatever path_audit() says,
        /// and its report is carried in the error message. That is the other half
        /// of the audit and the half that matters on Linux: there a
        /// differently-cased file is not a warning about some other machine, it is
        /// the reason this load failed, and the message says so instead of leaving
        /// the reader to compare two spellings by eye. It costs nothing on the
        /// path that works, since it only runs once the asset is already missing.
        std::expected<std::filesystem::path, core::Error> locate(
            std::string_view virtual_path) const;

        bool exists(std::string_view virtual_path) const
        {
            return this->locate(virtual_path).has_value();
        }

        std::expected<std::vector<std::byte>, core::Error> read(
            std::string_view virtual_path) const;

        std::expected<std::string, core::Error> read_text(
            std::string_view virtual_path) const;

        /// Turns the case audit on or off at runtime.
        ///
        /// Runtime rather than compile-time so that the tests can exercise the
        /// audit whatever build type they were compiled in — a diagnostic that is
        /// only tested in Debug is a diagnostic nobody has tested.
        void set_path_audit(const bool enabled) noexcept { this->audit_enabled = enabled; }
        bool path_audit() const noexcept { return this->audit_enabled; }

        /// Every mismatch found so far, in the order they were first met. One
        /// entry per virtual path: a file asked for sixty times a second is
        /// reported once.
        std::span<const CaseMismatchRecord> case_mismatches() const noexcept
        {
            return this->mismatches;
        }

        /// The closing summary, empty when there is nothing to say.
        ///
        /// The individual reports are printed when the mismatch is found, which
        /// can be thousands of lines before the game exits. This is what makes
        /// the problem impossible to have missed.
        std::string format_case_mismatch_summary() const;

    private:
        /// Audits a path that was found under `root`, at most once per path, and
        /// logs what it finds. Nothing else will report it: the load succeeded.
        void audit_loaded(std::string_view virtual_path,
                          const std::filesystem::path& root) const;

        /// The report for a path that was not found but exists under some root in
        /// a different case, empty when there is no such file. Not logged here —
        /// it travels in the error message instead, so it is reported once, by
        /// whoever decides how bad a missing asset is.
        std::string explain_missing(std::string_view virtual_path) const;

        /// Stores a mismatch for the closing summary. Answers false if this path
        /// was already recorded, which is what keeps a per-frame load from
        /// producing a per-frame diagnostic.
        bool record_mismatch(std::string_view virtual_path,
                             const std::filesystem::path& root,
                             std::vector<PathCaseMismatch> segments,
                             CaseMismatchOutcome outcome) const;

        std::vector<std::filesystem::path> mounted_roots;

        bool audit_enabled = DEFAULT_PATH_AUDIT;

        mutable std::unordered_set<std::string> audited_paths;
        mutable std::vector<CaseMismatchRecord> mismatches;
    };
}

#endif //CPEN_ASSETS_VIRTUAL_FILE_SYSTEM_HH
