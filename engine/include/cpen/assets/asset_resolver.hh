#ifndef CPEN_ASSETS_ASSET_RESOLVER_HH
#define CPEN_ASSETS_ASSET_RESOLVER_HH

#include "cpen/core/error.hh"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cpen::assets
{
    class VirtualFileSystem;

    /// What an asset is for, which is what decides where it lives and what file
    /// types it can be.
    ///
    /// The kind is always given by the caller rather than guessed from the
    /// identifier, because the caller always knows it: a script saying
    /// `scene room` is asking for a background and `show alice happy` for a
    /// sprite. Guessing would mean one namespace for every asset in the game, and
    /// a background and a character could not both be called "alice".
    enum class AssetKind : std::uint8_t
    {
        BACKGROUND,
        SPRITE,
        FONT,
        AUDIO,
        SCRIPT,
    };

    std::string_view to_string(AssetKind kind) noexcept;

    /// Where assets of this kind live, as a virtual path prefix.
    std::string_view directory_of(AssetKind kind) noexcept;

    /// The file extensions tried for this kind, in the order they are tried.
    ///
    /// Ordered by preference, not alphabetically: a lossless format is tried
    /// before a lossy one, so a PNG placed next to an older JPEG of the same
    /// picture wins.
    std::span<const std::string_view> extensions_of(AssetKind kind) noexcept;

    /// Builds the virtual path an identifier maps to by convention.
    ///
    /// Pure string work, and separate from the resolver for that reason: it
    /// answers "where would this be" without a file system to ask.
    std::string compose_virtual_path(AssetKind kind, std::string_view identifier,
                                     std::string_view extension);

    /// Checks that `identifier` is one.
    ///
    /// An identifier is what a script writes: "alice/happy", "chapter_01",
    /// "room". It groups with '/' and it carries no extension — the engine finds
    /// the file type, which is the entire point of naming assets this way, and an
    /// identifier that spells one would send the resolver looking for
    /// "alice.png.png".
    std::expected<void, core::Error> validate_asset_id(AssetKind kind,
                                                       std::string_view identifier);

    /// Turns the identifiers a game speaks into the virtual paths the file system
    /// understands.
    ///
    /// Convention first: "alice/happy" as a SPRITE is looked for at
    /// "assets/sprites/alice/happy" with each extension the kind allows, and the
    /// first file that exists answers. No registration, no manifest entry, no
    /// build step — a new sprite is a new file in the right directory, which is
    /// the property that makes writing a novel bearable.
    ///
    /// Manifest second: an alias overrides the convention for one identifier, and
    /// is consulted before it. That is the seam for everything a file name cannot
    /// carry — an asset kept somewhere unusual, one shared between two names, a
    /// packed atlas entry later. The manifest reader that fills these in comes
    /// with the configuration format; the aliases themselves work now.
    ///
    /// Holds a reference to the file system it probes, and must not outlive it.
    class AssetResolver
    {
    public:
        explicit AssetResolver(const VirtualFileSystem& files) noexcept
            : file_system(&files) {}

        /// Points one identifier at a virtual path of its own, extension and all.
        ///
        /// Refuses a path that is not a valid virtual path — a manifest is
        /// authored by hand, so this is a typo waiting to happen, and a silent
        /// one: the asset would simply never be found.
        std::expected<void, core::Error> add_alias(AssetKind kind,
                                                   std::string_view identifier,
                                                   std::string_view virtual_path);

        /// The virtual path for an identifier, or why there is none.
        ///
        /// The failure message lists every path that was tried. An engine that
        /// says only "asset not found" leaves the author to reconstruct the
        /// convention from memory, which they will get wrong in exactly the way
        /// that caused the failure.
        std::expected<std::string, core::Error> resolve(AssetKind kind,
                                                        std::string_view identifier) const;

    private:
        const VirtualFileSystem* file_system;

        /// Keyed by kind and identifier together: two kinds may use one name.
        std::unordered_map<std::string, std::string> aliases;

        /// Identifiers already complained about for having more than one file.
        mutable std::unordered_set<std::string> reported_ambiguities;
    };
}

#endif //CPEN_ASSETS_ASSET_RESOLVER_HH
