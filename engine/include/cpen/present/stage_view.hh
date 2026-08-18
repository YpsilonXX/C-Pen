#ifndef CPEN_PRESENT_STAGE_VIEW_HH
#define CPEN_PRESENT_STAGE_VIEW_HH

#include "cpen/assets/asset_manager.hh"
#include "cpen/present/layout.hh"
#include "cpen/present/stage.hh"
#include "cpen/render/texture.hh"

#include <glm/glm.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cpen::render
{
    class SpriteBatch;
}

namespace cpen::assets
{
    class AssetManager;
}

namespace cpen::present
{
    /// Draws a Stage.
    ///
    /// The other half of the model/view split: everything it needs to know about
    /// the story it reads out of a Stage, and everything it needs in order to
    /// draw — typefaces, pictures, a flat texture for the panels — it owns. The
    /// Stage never learns that any of this exists, which is what lets a whole
    /// story be run and checked with no window.
    ///
    /// Pictures are resolved through the asset manager and held for as long as
    /// what they draw is on the stage. That is not an optimisation: asking the
    /// manager for a name it cannot find records the failure, and a background
    /// requested on every frame would fill the ledger with sixty identical
    /// entries a second.
    class StageView
    {
    public:
        explicit StageView(assets::AssetManager& asset_manager, DialogueTheme dialogue_theme = {});

        StageView(const StageView&) = delete;
        StageView& operator=(const StageView&) = delete;

        /// Loads the typefaces and builds the flat texture the panels are drawn
        /// with. Needs a current GL context; failures are reported and survived,
        /// leaving the parts that did load drawable.
        void load();

        /// Releases everything the driver holds. Called from the owning state's
        /// on_exit(), while the context is still current — a GL object destroyed
        /// after the window is gone is destroyed against nothing.
        void release();

        /// Draws the whole stage into an open batch: background, sprites, the text
        /// box and its line, and the menu if one is on offer.
        ///
        /// `highlighted` is the choice under the pointer or on the keyboard's
        /// selection; the two are the same thing as far as drawing is concerned.
        void draw(render::SpriteBatch& batch, const Stage& stage, const glm::vec2& screen,
                  std::optional<std::size_t> highlighted = std::nullopt);

        /// Draws a block of text over the whole screen.
        ///
        /// What a reader is left looking at when a script will not compile or a
        /// story faults. It belongs here rather than in the state that produces
        /// the message because the typefaces and the panel are here, and because
        /// a message that cannot be drawn is a message nobody reads.
        void draw_message(render::SpriteBatch& batch, const glm::vec2& screen,
                          std::string_view text);

        const DialogueTheme& theme() const noexcept { return this->dialogue_theme; }

        /// Non-const so a game can retune the look while running, which is what
        /// makes a theme worth having as a value.
        DialogueTheme& theme() noexcept { return this->dialogue_theme; }

        /// True once the typefaces are in hand. Drawing without them puts the
        /// pictures up and leaves the words out rather than refusing the frame.
        bool has_text() const noexcept { return this->body_font && this->speaker_font; }

    private:
        /// One picture the stage is currently asking for.
        ///
        /// A failure is remembered as a failure so that the manager is asked once:
        /// the placeholder is drawn in its place, and the missing-asset ledger
        /// gets one entry rather than one per frame.
        struct Picture
        {
            assets::TextureReference reference{};
            bool failed = false;
        };

        /// The texture for an asset name, loading it the first time it is asked
        /// for. Null when there is nothing at all to draw, not even a placeholder.
        const render::Texture* picture(assets::AssetKind kind, const std::string& identifier);

        /// Drops the pictures nothing on the stage refers to any more.
        void forget_unused(const Stage& stage);

        void draw_panel(render::SpriteBatch& batch, const Rectangle& area,
                        const glm::vec4& color);

        void draw_background(render::SpriteBatch& batch, const Stage& stage,
                             const glm::vec2& screen);
        void draw_sprites(render::SpriteBatch& batch, const Stage& stage,
                          const glm::vec2& screen);
        void draw_textbox(render::SpriteBatch& batch, const Stage& stage,
                          const glm::vec2& screen);
        void draw_menu(render::SpriteBatch& batch, const Stage& stage, const glm::vec2& screen,
                       std::optional<std::size_t> highlighted);

        assets::AssetManager* assets;
        DialogueTheme dialogue_theme;

        assets::FontReference body_font;
        assets::FontReference speaker_font;

        /// One white texel, which is how a solid panel is drawn by a renderer that
        /// only knows how to draw textured quads: the sprite's colour does the
        /// work and the texture contributes nothing.
        std::optional<render::Texture> panel_texture;

        std::unordered_map<std::string, Picture> pictures;
    };
}

#endif //CPEN_PRESENT_STAGE_VIEW_HH
