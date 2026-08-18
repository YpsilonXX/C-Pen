#include "cpen/assets/virtual_file_system.hh"

#include "cpen/core/file_system.hh"
#include "cpen/core/log.hh"

#include <format>
#include <system_error>
#include <utility>

namespace cpen::assets
{
    namespace
    {
        std::string describe_roots(const std::span<const std::filesystem::path> roots)
        {
            if (roots.empty())
            {
                return "no roots are mounted";
            }

            std::string listed = "searched";

            for (const std::filesystem::path& root : roots)
            {
                listed += std::format(" '{}'", core::path_to_utf8(root));
            }

            return listed;
        }
    }

    void VirtualFileSystem::mount(std::filesystem::path root)
    {
        std::error_code error;

        // Absolute and symlink-free where possible, so that every later message
        // names a path the reader can paste into a terminal. weakly_canonical
        // tolerates a root that is not there yet, which is the case the warning
        // below is about.
        std::filesystem::path resolved = std::filesystem::weakly_canonical(root, error);

        if (error)
        {
            resolved = std::move(root);
        }

        if (!std::filesystem::is_directory(resolved, error))
        {
            log::warn(log::Category::ASSETS,
                      "mounted '{}', which is not a directory; every asset looked for "
                      "there will be reported as missing",
                      core::path_to_utf8(resolved));
        }
        else
        {
            log::info(log::Category::ASSETS, "mounted '{}'", core::path_to_utf8(resolved));
        }

        this->mounted_roots.push_back(std::move(resolved));
    }

    std::expected<std::filesystem::path, core::Error> VirtualFileSystem::locate(
        const std::string_view virtual_path) const
    {
        if (const std::expected<void, core::Error> valid = validate_virtual_path(virtual_path);
            !valid.has_value())
        {
            return std::unexpected(valid.error());
        }

        const std::filesystem::path relative = core::path_from_utf8(virtual_path);

        for (const std::filesystem::path& root : this->mounted_roots)
        {
            std::filesystem::path candidate = root / relative;

            std::error_code error;
            if (!std::filesystem::is_regular_file(candidate, error))
            {
                continue;
            }

            if (this->audit_enabled)
            {
                this->audit_loaded(virtual_path, root);
            }

            return candidate;
        }

        return std::unexpected(core::make_error(
            core::ErrorCode::FILE_NOT_FOUND, "no asset '{}' ({}){}",
            virtual_path, describe_roots(this->mounted_roots),
            this->explain_missing(virtual_path)));
    }

    std::expected<std::vector<std::byte>, core::Error> VirtualFileSystem::read(
        const std::string_view virtual_path) const
    {
        return this->locate(virtual_path).and_then(core::read_file_bytes);
    }

    std::expected<std::string, core::Error> VirtualFileSystem::read_text(
        const std::string_view virtual_path) const
    {
        return this->locate(virtual_path).and_then(core::read_file_text);
    }

    void VirtualFileSystem::audit_loaded(const std::string_view virtual_path,
                                         const std::filesystem::path& root) const
    {
        // Once per path. The alternative is the same wall of text every frame for
        // an asset loaded in a loop, which teaches the reader to scroll past
        // exactly the thing they must not scroll past.
        if (this->audited_paths.contains(std::string{virtual_path}))
        {
            return;
        }

        std::vector<PathCaseMismatch> found = audit_path_case(root, virtual_path);

        if (found.empty())
        {
            // Nothing wrong with this path; remembering it saves the directory
            // scan the next time it is asked for.
            this->audited_paths.emplace(virtual_path);
            return;
        }

        const std::string report = format_case_mismatch_report(
            virtual_path, found, CaseMismatchOutcome::LOADED_ANYWAY);

        this->record_mismatch(virtual_path, root, std::move(found),
                              CaseMismatchOutcome::LOADED_ANYWAY);

        log::error(log::Category::ASSETS, "{}", report);
    }

    std::string VirtualFileSystem::explain_missing(const std::string_view virtual_path) const
    {
        // A path asked for every frame and missing every frame must not cost a
        // directory scan every frame: what was found the first time is still true.
        for (const CaseMismatchRecord& record : this->mismatches)
        {
            if (record.virtual_path == virtual_path)
            {
                return format_case_mismatch_report(virtual_path, record.segments,
                                                   CaseMismatchOutcome::NOT_FOUND);
            }
        }

        for (const std::filesystem::path& root : this->mounted_roots)
        {
            std::vector<PathCaseMismatch> found = audit_path_case(root, virtual_path);

            if (found.empty())
            {
                continue;
            }

            std::string report = format_case_mismatch_report(
                virtual_path, found, CaseMismatchOutcome::NOT_FOUND);

            this->record_mismatch(virtual_path, root, std::move(found),
                                  CaseMismatchOutcome::NOT_FOUND);

            return report;
        }

        return {};
    }

    bool VirtualFileSystem::record_mismatch(const std::string_view virtual_path,
                                            const std::filesystem::path& root,
                                            std::vector<PathCaseMismatch> segments,
                                            const CaseMismatchOutcome outcome) const
    {
        const auto [entry, inserted] = this->audited_paths.emplace(virtual_path);

        if (!inserted)
        {
            return false;
        }

        this->mismatches.push_back(CaseMismatchRecord{
            .virtual_path = std::string{virtual_path},
            .root = root,
            .segments = std::move(segments),
            .outcome = outcome,
        });

        return true;
    }

    std::string VirtualFileSystem::format_case_mismatch_summary() const
    {
        if (this->mismatches.empty())
        {
            return {};
        }

        std::string summary = std::format(
            "\n"
            "!!! ==================================================================== !!!\n"
            "!!!  {} ASSET NAME(S) DIFFER FROM THE FILES ON DISK ONLY IN CASE\n"
            "!!!  The same game does not run on Windows and on Linux until they agree.\n"
            "!!! ==================================================================== !!!\n",
            this->mismatches.size());

        for (const CaseMismatchRecord& record : this->mismatches)
        {
            summary += std::format("  asked for '{}' ({})\n", record.virtual_path,
                                   record.outcome == CaseMismatchOutcome::LOADED_ANYWAY
                                       ? "loaded here, missing on a case-sensitive system"
                                       : "not loaded");

            for (const PathCaseMismatch& mismatch : record.segments)
            {
                summary += std::format("      '{}' is spelled '{}' under '{}'\n",
                                       mismatch.requested, mismatch.actual,
                                       core::path_to_utf8(record.root));
            }
        }

        summary +=
            "!!! ==================================================================== !!!\n";

        return summary;
    }
}
