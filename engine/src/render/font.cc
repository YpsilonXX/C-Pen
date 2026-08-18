#include "cpen/render/font.hh"

#include "cpen/core/file_system.hh"
#include "cpen/core/log.hh"
#include "cpen/render/pixel_format.hh"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace cpen::render
{
    namespace detail
    {
        /// Owns the FreeType library handle for as long as any Font needs it.
        ///
        /// Shared by reference count rather than kept in a function-local static.
        /// A static would be destroyed after main returns, which is after every
        /// Font that a game state owned — but not necessarily after one a caller
        /// left in a static of their own, and a Font outliving the library that
        /// made its face is a crash at exit with nothing in the log. Counting
        /// references makes the order a fact rather than a hope, in the same shape
        /// as core::Subscription holding a weak_ptr to its bus.
        class FreeTypeLibrary
        {
        public:
            FreeTypeLibrary() = default;

            ~FreeTypeLibrary()
            {
                if (this->handle != nullptr)
                {
                    FT_Done_FreeType(this->handle);
                }
            }

            FreeTypeLibrary(const FreeTypeLibrary&) = delete;
            FreeTypeLibrary& operator=(const FreeTypeLibrary&) = delete;

            FT_Library handle = nullptr;
        };
    }

    namespace
    {
        /// The library, created on first use and released when the last Font goes.
        std::shared_ptr<detail::FreeTypeLibrary> acquire_library()
        {
            static std::weak_ptr<detail::FreeTypeLibrary> existing;

            if (std::shared_ptr<detail::FreeTypeLibrary> live = existing.lock())
            {
                return live;
            }

            auto created = std::make_shared<detail::FreeTypeLibrary>();
            if (FT_Init_FreeType(&created->handle) != 0)
            {
                return nullptr;
            }

            existing = created;
            return created;
        }

        /// One texel of empty space on the right and below every glyph.
        ///
        /// The atlas is sampled with linear filtering, because text is drawn
        /// through the same letterbox scaling as everything else and is almost
        /// never at exactly one texel per pixel. Linear filtering reads the
        /// neighbouring texels, so without a gap the right-hand edge of one glyph
        /// bleeds a grey fringe onto the left of the next.
        constexpr std::uint32_t GLYPH_PADDING = 1;

        /// FreeType reports metrics in 26.6 fixed point: sixty-fourths of a pixel.
        constexpr float from_fixed_point(const long value) noexcept
        {
            return static_cast<float>(value) / 64.0f;
        }

        /// Copies a FreeType bitmap into a tightly packed buffer.
        ///
        /// Row by row through `pitch` rather than as one block: FreeType pads rows
        /// to a boundary of its own choosing, and although a grey bitmap usually
        /// comes back with pitch equal to width, nothing promises it. A negative
        /// pitch means the rows are stored bottom upwards, which this refuses
        /// rather than silently drawing the glyph upside down — it cannot arise
        /// from FT_RENDER_MODE_NORMAL, and guessing at one would be worse than
        /// saying so.
        std::vector<std::byte> pack_bitmap(const FT_Bitmap& bitmap)
        {
            const auto width = static_cast<std::size_t>(bitmap.width);
            const auto height = static_cast<std::size_t>(bitmap.rows);

            std::vector<std::byte> packed(width * height);

            for (std::size_t row = 0; row < height; ++row)
            {
                const unsigned char* const source =
                    bitmap.buffer + row * static_cast<std::size_t>(bitmap.pitch);

                for (std::size_t column = 0; column < width; ++column)
                {
                    packed[row * width + column] = static_cast<std::byte>(source[column]);
                }
            }

            return packed;
        }
    }

    std::expected<Font, core::Error> Font::from_file(const std::filesystem::path& path,
                                                     const std::uint32_t pixel_size,
                                                     const FontConfig& config)
    {
        // Reading the file here rather than handing the path to FT_New_Face buys
        // two things: one loading path for every typeface however it arrived, and
        // error messages that distinguish a missing file from an unreadable one,
        // which FreeType's single error code does not.
        return core::read_file_bytes(path).and_then(
            [&](const std::vector<std::byte>& data)
            {
                return Font::from_memory(data, pixel_size, config,
                                         core::path_to_utf8(path));
            });
    }

    std::expected<Font, core::Error> Font::from_memory(const std::span<const std::byte> data,
                                                       const std::uint32_t pixel_size,
                                                       const FontConfig& config,
                                                       const std::string_view description)
    {
        if (pixel_size == 0)
        {
            return std::unexpected(core::make_error(core::ErrorCode::INVALID_FORMAT,
                                                    "font '{}': a pixel size of zero has no "
                                                    "glyphs to rasterise",
                                                    description));
        }

        std::shared_ptr<detail::FreeTypeLibrary> library = acquire_library();
        if (!library)
        {
            return std::unexpected(core::make_error(core::ErrorCode::UNSPECIFIED,
                                                    "FreeType could not be initialised"));
        }

        // Copied, not referenced: FreeType reads from this buffer for as long as
        // the face lives, and the caller's span is whatever it happened to be —
        // often a temporary the asset layer is about to drop.
        std::vector<std::byte> owned(data.begin(), data.end());

        FT_Face face = nullptr;
        if (FT_New_Memory_Face(library->handle,
                               reinterpret_cast<const FT_Byte*>(owned.data()),
                               static_cast<FT_Long>(owned.size()), 0, &face) != 0)
        {
            return std::unexpected(core::make_error(core::ErrorCode::INVALID_FORMAT,
                                                    "font '{}' could not be read as a typeface",
                                                    description));
        }

        // Width zero means "same as the height", which is what a proportional
        // typeface wants; giving both would distort it.
        if (FT_Set_Pixel_Sizes(face, 0, pixel_size) != 0)
        {
            FT_Done_Face(face);
            return std::unexpected(core::make_error(
                core::ErrorCode::INVALID_FORMAT,
                "font '{}' does not support a size of {} pixel(s); it is most likely a "
                "bitmap typeface with fixed sizes only",
                description, pixel_size));
        }

        auto atlas = Texture::storage(config.atlas_size, config.atlas_size, PixelFormat::R8,
                                      TextureConfig{
                                          .minify_filter = TextureFilter::LINEAR,
                                          .magnify_filter = TextureFilter::LINEAR,
                                      });
        if (!atlas)
        {
            FT_Done_Face(face);
            return std::unexpected(atlas.error());
        }

        Font font{std::move(library), face, std::move(owned), std::move(*atlas), pixel_size,
                  config.atlas_size};

        log::info(log::Category::RENDER,
                  "font '{}' loaded at {} pixel(s): line height {}, ascender {}, "
                  "descender {}, atlas {}x{}",
                  font.family_name(), pixel_size, font.line_height(), font.ascender(),
                  font.descender(), config.atlas_size, config.atlas_size);

        return font;
    }

    Font::Font(std::shared_ptr<detail::FreeTypeLibrary> owning_library,
               FT_FaceRec_* const loaded_face, std::vector<std::byte> data, Texture texture,
               const std::uint32_t pixel_size, const std::uint32_t atlas_size)
        : library(std::move(owning_library)),
          face(loaded_face),
          face_data(std::move(data)),
          atlas_texture(std::move(texture)),
          atlas_extent(atlas_size),
          size_in_pixels(pixel_size)
    {
        const FT_Size_Metrics& metrics = this->face->size->metrics;

        this->metrics_line_height = from_fixed_point(metrics.height);
        this->metrics_ascender = from_fixed_point(metrics.ascender);

        // FreeType reports the descender as a negative offset from the baseline.
        // Both are lengths here, so that a line box is ascender + descender without
        // the reader having to remember which one is signed.
        this->metrics_descender = -from_fixed_point(metrics.descender);

        if (this->face->family_name != nullptr)
        {
            this->family = this->face->family_name;
        }
    }

    Font::~Font()
    {
        this->destroy();
    }

    Font::Font(Font&& other) noexcept
        : library(std::move(other.library)),
          face(std::exchange(other.face, nullptr)),
          face_data(std::move(other.face_data)),
          atlas_texture(std::move(other.atlas_texture)),
          glyphs(std::move(other.glyphs)),
          atlas_extent(other.atlas_extent),
          shelf_x(other.shelf_x),
          shelf_y(other.shelf_y),
          shelf_height(other.shelf_height),
          size_in_pixels(other.size_in_pixels),
          metrics_line_height(other.metrics_line_height),
          metrics_ascender(other.metrics_ascender),
          metrics_descender(other.metrics_descender),
          family(std::move(other.family)),
          reported_full(other.reported_full)
    {
    }

    Font& Font::operator=(Font&& other) noexcept
    {
        if (this != &other)
        {
            this->destroy();

            this->library = std::move(other.library);
            this->face = std::exchange(other.face, nullptr);
            this->face_data = std::move(other.face_data);
            this->atlas_texture = std::move(other.atlas_texture);
            this->glyphs = std::move(other.glyphs);
            this->atlas_extent = other.atlas_extent;
            this->shelf_x = other.shelf_x;
            this->shelf_y = other.shelf_y;
            this->shelf_height = other.shelf_height;
            this->size_in_pixels = other.size_in_pixels;
            this->metrics_line_height = other.metrics_line_height;
            this->metrics_ascender = other.metrics_ascender;
            this->metrics_descender = other.metrics_descender;
            this->family = std::move(other.family);
            this->reported_full = other.reported_full;
        }
        return *this;
    }

    void Font::destroy() noexcept
    {
        if (this->face != nullptr)
        {
            FT_Done_Face(this->face);
            this->face = nullptr;
        }

        // Only after the face is gone: FreeType was reading from this buffer.
        this->face_data.clear();
        this->face_data.shrink_to_fit();

        // The library reference goes last, and only through this member: the face
        // above was made by it and must not outlive it.
        this->library.reset();
    }

    const Glyph* Font::glyph(const char32_t code_point)
    {
        const auto found = this->glyphs.find(code_point);
        if (found != this->glyphs.end())
        {
            return &found->second;
        }

        return this->rasterize(code_point);
    }

    bool Font::reserve(const std::uint32_t width, const std::uint32_t height,
                       glm::vec2& position)
    {
        if (width > this->atlas_extent || height > this->atlas_extent)
        {
            return false;
        }

        // Start a new shelf when the current one has no room along its length.
        if (this->shelf_x + width > this->atlas_extent)
        {
            this->shelf_y += this->shelf_height + GLYPH_PADDING;
            this->shelf_x = 0;
            this->shelf_height = 0;
        }

        if (this->shelf_y + height > this->atlas_extent)
        {
            return false;
        }

        position = glm::vec2{static_cast<float>(this->shelf_x),
                             static_cast<float>(this->shelf_y)};

        this->shelf_x += width + GLYPH_PADDING;
        this->shelf_height = std::max(this->shelf_height, height);

        return true;
    }

    const Glyph* Font::rasterize(const char32_t code_point)
    {
        // FT_LOAD_RENDER asks for the outline to be scan-converted in one step. A
        // code point the typeface does not have loads glyph index zero, which is
        // the typeface's own missing-character box: visible, and far more use to
        // whoever has to notice it than a gap in the line would be.
        if (FT_Load_Char(this->face, static_cast<FT_ULong>(code_point), FT_LOAD_RENDER) != 0)
        {
            log::error(log::Category::RENDER, "font '{}': U+{:04X} could not be rasterised",
                       this->family, static_cast<std::uint32_t>(code_point));
            return nullptr;
        }

        const FT_GlyphSlot slot = this->face->glyph;
        const FT_Bitmap& bitmap = slot->bitmap;

        Glyph glyph{
            .region = {},
            .bearing = {static_cast<float>(slot->bitmap_left),
                        // Negated: FreeType measures upwards from the baseline and
                        // virtual space measures downwards.
                        -static_cast<float>(slot->bitmap_top)},
            .size = {static_cast<float>(bitmap.width), static_cast<float>(bitmap.rows)},
            .advance = from_fixed_point(slot->advance.x),
        };

        // A space has an advance and nothing to draw. Recorded with an empty region
        // so that the lookup still hits and the pen still moves.
        if (bitmap.width == 0 || bitmap.rows == 0)
        {
            const auto inserted = this->glyphs.emplace(code_point, glyph);
            return &inserted.first->second;
        }

        if (bitmap.pitch < 0)
        {
            log::error(log::Category::RENDER,
                       "font '{}': U+{:04X} came back stored bottom upwards, which this "
                       "loader does not handle; the glyph is skipped",
                       this->family, static_cast<std::uint32_t>(code_point));
            return nullptr;
        }

        glm::vec2 position{0.0f, 0.0f};
        if (!this->reserve(bitmap.width, bitmap.rows, position))
        {
            if (!this->reported_full)
            {
                this->reported_full = true;
                log::error(log::Category::RENDER,
                           "font '{}': the {}x{} atlas is full after {} glyph(s); further "
                           "characters will not be drawn. A larger FontConfig::atlas_size "
                           "is the answer",
                           this->family, this->atlas_extent, this->atlas_extent,
                           this->glyphs.size());
            }
            return nullptr;
        }

        const std::vector<std::byte> packed = pack_bitmap(bitmap);

        this->atlas_texture.update(packed, static_cast<std::uint32_t>(position.x),
                                   static_cast<std::uint32_t>(position.y), bitmap.width,
                                   bitmap.rows);

        glyph.region = TextureRegion{.position = position, .size = glyph.size};

        const auto inserted = this->glyphs.emplace(code_point, glyph);
        return &inserted.first->second;
    }
}
