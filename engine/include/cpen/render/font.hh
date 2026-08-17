#ifndef CPEN_RENDER_FONT_HH
#define CPEN_RENDER_FONT_HH

#include "cpen/core/error.hh"
#include "cpen/render/sprite.hh"
#include "cpen/render/texture.hh"

#include <glm/glm.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string_view>
#include <unordered_map>

/// Forward-declared, not included: FreeType is an implementation detail of this
/// class exactly as GLFW is of platform::Window, and FT_Face is a pointer to this.
struct FT_FaceRec_;

namespace cpen::render
{
    namespace detail
    {
        /// Owns the process's FreeType library handle. Defined in font.cc.
        class FreeTypeLibrary;
    }

    /// One rasterised character: where it sits in the atlas and how to place it.
    struct Glyph
    {
        /// The glyph's rectangle in the atlas, in texels. Empty for a character
        /// with nothing to draw, such as a space.
        TextureRegion region;

        /// Offset from the pen position to the top-left corner of the glyph's
        /// rectangle, in pixels.
        ///
        /// FreeType measures the vertical part upwards from the baseline; this is
        /// already negated, so that adding it to a pen position in virtual space —
        /// where y grows downwards — gives the corner directly.
        glm::vec2 bearing{0.0f, 0.0f};

        /// The glyph's extent in pixels, which is the size a sprite drawing it must
        /// have to appear at its intended scale.
        glm::vec2 size{0.0f, 0.0f};

        /// How far the pen moves along the line after this glyph. Includes the side
        /// bearings, so it is wider than `size`.
        float advance = 0.0f;
    };

    struct FontConfig
    {
        /// Side of the square atlas texture, in texels.
        ///
        /// 512 holds several hundred glyphs at ordinary reading sizes, which covers
        /// Latin and Cyrillic together with room to spare. A font that runs out
        /// says so in the log rather than drawing nonsense.
        std::uint32_t atlas_size = 512;
    };

    /// One typeface at one size, with the atlas of the glyphs drawn from it so far.
    ///
    /// Size is part of what a Font *is*, not a parameter to its methods: FreeType
    /// rasterises at a fixed pixel size, and a glyph drawn at one size and scaled
    /// to another is the blurry result the whole atlas exists to avoid. Two sizes
    /// of one typeface are two Fonts with two atlases, and the sprite batch draws
    /// them as two runs — which is also how two different typefaces behave, so
    /// switching font and switching size cost the same and work the same way.
    ///
    /// Nothing here is global and nothing is current: every function that draws or
    /// measures text is handed the Font to use. A game holding several and choosing
    /// between them per call is the intended shape, and is what makes changing the
    /// font at runtime a matter of passing a different object rather than of
    /// reconfiguring anything.
    ///
    /// Glyphs are rasterised the first time they are asked for and kept
    /// thereafter. That is what lets any text be drawn — a name the player typed,
    /// a script that arrived after the build — at the cost of an occasional upload
    /// mid-frame the first time a character is seen.
    ///
    /// Move-only, and must not outlive the GL context: it owns a Texture.
    class Font
    {
    public:
        /// Loads a typeface from a file at `pixel_size` pixels.
        ///
        /// The size is in pixels rather than points because there is no meaningful
        /// physical size to convert against: the engine draws into a virtual
        /// 1920x1080 space that is then letterboxed, so a "12 point" glyph would be
        /// twelve points of a screen nobody can measure.
        static std::expected<Font, core::Error> from_file(const std::filesystem::path& path,
                                                          std::uint32_t pixel_size,
                                                          const FontConfig& config = {});

        ~Font();

        Font(Font&& other) noexcept;
        Font& operator=(Font&& other) noexcept;

        Font(const Font&) = delete;
        Font& operator=(const Font&) = delete;

        /// The glyph for `code_point`, rasterising it if this is the first time.
        ///
        /// Returns nullptr only when the atlas is full, which is reported once. A
        /// character the typeface does not have is not an error: FreeType answers
        /// with the typeface's own missing-character glyph, which is a visible box
        /// and considerably more useful than a gap.
        ///
        /// Not const, and not merely because of the cache: rasterising uploads to
        /// the atlas texture, so asking for a glyph really can change what this
        /// object owns. Every function that measures text therefore takes a
        /// non-const Font too, since measuring needs the advances.
        const Glyph* glyph(char32_t code_point);

        /// Distance from one baseline to the next, in pixels.
        ///
        /// The typeface's own recommendation, and only that. It is *not*
        /// guaranteed to be as large as ascender() + descender(), and commonly is
        /// not — DejaVu Sans at 24 pixels reports an ascender of 23 and a descender
        /// of 6 against a line height of 28. The figure is a designer's judgement
        /// about how the face reads in a paragraph rather than a bound on how far
        /// its outlines reach, so a caller that needs lines which provably cannot
        /// touch has to take the maximum of the two itself.
        float line_height() const noexcept { return this->metrics_line_height; }

        /// Distance from the baseline up to the top of the line box, positive.
        float ascender() const noexcept { return this->metrics_ascender; }

        /// Distance from the baseline down to the bottom of the line box, positive
        /// — negated from FreeType's own sign so that both are lengths.
        float descender() const noexcept { return this->metrics_descender; }

        std::uint32_t pixel_size() const noexcept { return this->size_in_pixels; }

        /// The atlas the glyphs live in. Single-channel: a glyph is coverage, not
        /// colour, and the sprite batch reads it as white at that coverage so the
        /// tint decides what colour the text is.
        const Texture& atlas() const noexcept { return this->atlas_texture; }

        /// Glyphs rasterised so far.
        std::size_t glyph_count() const noexcept { return this->glyphs.size(); }

        /// The typeface's own name, for logs. Empty if the file did not carry one.
        std::string_view family_name() const noexcept { return this->family; }

    private:
        Font(std::shared_ptr<detail::FreeTypeLibrary> owning_library, FT_FaceRec_* loaded_face,
             Texture texture, std::uint32_t pixel_size, std::uint32_t atlas_size);

        /// Rasterises `code_point` and copies it into the atlas.
        const Glyph* rasterize(char32_t code_point);

        /// Finds room for a `width` x `height` rectangle, advancing the packer.
        /// Returns false when the atlas has no room left.
        bool reserve(std::uint32_t width, std::uint32_t height, glm::vec2& position);

        void destroy() noexcept;

        std::shared_ptr<detail::FreeTypeLibrary> library;
        FT_FaceRec_* face = nullptr;

        Texture atlas_texture;
        std::unordered_map<char32_t, Glyph> glyphs;

        // A shelf packer: glyphs are laid left to right along a row whose height is
        // that of the tallest glyph in it, and a new row starts below when the
        // current one runs out. Wasteful in general and close to optimal here,
        // because glyphs of one size are of nearly one height.
        std::uint32_t atlas_extent = 0;
        std::uint32_t shelf_x = 0;
        std::uint32_t shelf_y = 0;
        std::uint32_t shelf_height = 0;

        std::uint32_t size_in_pixels = 0;
        float metrics_line_height = 0.0f;
        float metrics_ascender = 0.0f;
        float metrics_descender = 0.0f;

        std::string family;

        /// Set once the atlas has been reported full, so a page of text that no
        /// longer fits complains once rather than once per character.
        bool reported_full = false;
    };
}

#endif //CPEN_RENDER_FONT_HH
