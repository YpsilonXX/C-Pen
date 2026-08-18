#include <catch2/catch_test_macros.hpp>

#include "cpen/app/asset_roots.hh"
#include "cpen/assets/asset_manager.hh"
#include "cpen/assets/asset_resolver.hh"
#include "cpen/assets/virtual_file_system.hh"
#include "cpen/present/layout.hh"
#include "cpen/present/stage.hh"
#include "cpen/present/stage_view.hh"
#include "cpen/render/sprite_batch.hh"
#include "cpen/render/viewport.hh"
#include "cpen/script/command_sink.hh"
#include "support/gl_fixture.hh"
#include "support/log_capture.hh"
#include "support/render_target.hh"
#include "support/temporary_directory.hh"
#include "support/trace.hh"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

using cpen::assets::AssetManager;
using cpen::assets::AssetResolver;
using cpen::assets::VirtualFileSystem;
using cpen::present::Anchor;
using cpen::present::DialogueTheme;
using cpen::present::Rectangle;
using cpen::present::Stage;
using cpen::present::StageView;
using cpen::present::position_of;
using cpen::present::sprite_rectangle;
using cpen::present::textbox_rectangle;
using cpen::render::SpriteBatch;
using cpen::render::Viewport;
using cpen::script::MenuCommand;
using cpen::script::SayCommand;
using cpen::script::SceneCommand;
using cpen::script::ShowCommand;
using cpen::test::gl_context;
using cpen::test::LogCaptureGuard;
using cpen::test::RenderTarget;
using cpen::test::TemporaryDirectory;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    /// A square target, and a virtual screen exactly its size, so that a virtual
    /// coordinate is a pixel and the layout can be checked by looking at one.
    constexpr int TARGET_SIZE = 256;
    constexpr glm::vec2 SCREEN{static_cast<float>(TARGET_SIZE), static_cast<float>(TARGET_SIZE)};

    constexpr glm::vec4 BLACK{0.0f, 0.0f, 0.0f, 1.0f};

    /// Colours nothing else can produce, so that a pixel names the thing that drew
    /// it. Opaque, because a translucent panel over black would come back scaled
    /// by its own alpha and every assertion would carry the arithmetic.
    constexpr glm::vec4 BOX_RED{1.0f, 0.0f, 0.0f, 1.0f};
    constexpr glm::vec4 CHOICE_BLUE{0.0f, 0.0f, 1.0f, 1.0f};
    constexpr glm::vec4 CHOICE_GREEN{0.0f, 1.0f, 0.0f, 1.0f};

    /// The theme the cases below are measured against. The defaults are sized for
    /// a screen seven times wider than this one, which is exactly why a theme is a
    /// value and not a set of constants in the drawing code.
    DialogueTheme test_theme()
    {
        DialogueTheme theme;

        theme.box_margin = 8.0f;
        theme.box_height = 60.0f;
        theme.box_padding = 6.0f;
        theme.body_pixel_size = 16;
        theme.speaker_pixel_size = 18;
        theme.box_color = BOX_RED;

        theme.choice_width = 200.0f;
        theme.choice_height = 30.0f;
        theme.choice_spacing = 10.0f;
        theme.choice_color = CHOICE_BLUE;
        theme.choice_highlight_color = CHOICE_GREEN;

        return theme;
    }

    /// Everything under the view, assembled as the Application assembles it. The
    /// engine's root is mounted as well as the game's, because the typeface the
    /// dialogue is set in is the engine's.
    struct Fixture
    {
        TemporaryDirectory directory;
        VirtualFileSystem files;
        AssetResolver resolver;
        AssetManager assets;
        Stage stage;
        StageView view;

        Fixture() : resolver(files), assets(files, resolver), view(assets, test_theme())
        {
            this->files.mount(this->directory.path());

            const auto roots = cpen::app::default_asset_roots();
            REQUIRE(roots.has_value());
            this->files.mount(roots->engine);

            this->stage.set_reveal_speed(0.0);
            this->view.load();
        }

        ~Fixture() { this->view.release(); }

        Fixture(const Fixture&) = delete;
        Fixture& operator=(const Fixture&) = delete;
    };

    /// glReadPixels counts rows from the bottom; virtual space counts them from the
    /// top. Converted here, once and by name, so every case reads in the
    /// coordinates the layout is written in.
    std::array<std::uint8_t, 4> pixel_at_virtual(const RenderTarget& target, const int x,
                                                 const int y)
    {
        return target.pixel_at(x, target.size() - 1 - y);
    }

    std::array<std::uint8_t, 4> pixel_at_virtual(const RenderTarget& target,
                                                 const glm::vec2& point)
    {
        return pixel_at_virtual(target, static_cast<int>(point.x), static_cast<int>(point.y));
    }

    bool is_colour(const std::array<std::uint8_t, 4>& pixel, const glm::vec4& expected,
                   const int tolerance = 8)
    {
        const std::array<int, 3> wanted = {
            static_cast<int>(expected.r * 255.0f),
            static_cast<int>(expected.g * 255.0f),
            static_cast<int>(expected.b * 255.0f),
        };

        for (std::size_t channel = 0; channel < wanted.size(); ++channel)
        {
            const int difference = static_cast<int>(pixel[channel]) - wanted[channel];

            if (difference > tolerance || difference < -tolerance)
            {
                return false;
            }
        }

        return true;
    }

    void trace_pixel(const char* const label, const std::array<std::uint8_t, 4>& color)
    {
        trace("{}: r {}, g {}, b {}, a {}", label, static_cast<int>(color[0]),
              static_cast<int>(color[1]), static_cast<int>(color[2]),
              static_cast<int>(color[3]));
    }

    /// Draws one frame of the stage into the target and returns how many sprites
    /// the batch was given.
    std::size_t draw_frame(Fixture& fixture, const std::optional<std::size_t> highlighted = {})
    {
        auto batch = SpriteBatch::create();
        REQUIRE(batch.has_value());

        const Viewport viewport{TARGET_SIZE, TARGET_SIZE};

        cpen::render::clear(BLACK);

        batch->begin(viewport.projection());
        fixture.view.draw(*batch, fixture.stage, SCREEN, highlighted);
        batch->end();

        return batch->sprite_count();
    }

    /// The middle of a choice's panel, from the same function that drew it.
    glm::vec2 centre_of_choice(const std::size_t index, const std::size_t count)
    {
        const auto rectangles = choice_rectangles(test_theme(), SCREEN, count);
        return rectangles[index].position + rectangles[index].size * 0.5f;
    }
}

TEST_CASE("the text box is drawn where the layout says it is", "[present][view][gpu]")
{
    gl_context();

    const RenderTarget target(TARGET_SIZE);
    REQUIRE(target.is_complete());

    Fixture fixture;
    fixture.stage.say(SayCommand{.text = "Привет."});

    draw_frame(fixture);

    const Rectangle box = textbox_rectangle(test_theme(), SCREEN);
    trace("the box is at ({}, {}), {}x{}", box.position.x, box.position.y, box.size.x,
          box.size.y);

    // Inside the box, at a corner rather than the middle, because the middle is
    // where the text is and a glyph would answer for the panel.
    const auto inside = pixel_at_virtual(target, box.position + glm::vec2{4.0f, 4.0f});
    const auto above = pixel_at_virtual(target, box.position + glm::vec2{4.0f, -6.0f});

    trace_pixel("inside the box", inside);
    trace_pixel("just above it", above);

    CHECK(is_colour(inside, BOX_RED));
    CHECK(is_colour(above, BLACK));
}

TEST_CASE("nothing said means no text box at all", "[present][view][gpu]")
{
    gl_context();

    const RenderTarget target(TARGET_SIZE);
    REQUIRE(target.is_complete());

    Fixture fixture;

    // An empty stage draws nothing whatever, rather than an empty box waiting for
    // a line: what is on screen before a story says anything is the story's
    // business, and a box the script never asked for is furniture.
    CHECK(draw_frame(fixture) == 0);

    CHECK(is_colour(target.centre_pixel(), BLACK));
}

TEST_CASE("the line is drawn inside its box", "[present][view][gpu]")
{
    gl_context();

    const RenderTarget target(TARGET_SIZE);
    REQUIRE(target.is_complete());

    Fixture fixture;
    fixture.stage.say(SayCommand{.text = "ЖЖЖЖЖЖЖЖ"});

    const std::size_t drawn = draw_frame(fixture);

    // The panel and one sprite per glyph: text is not a second renderer, it is
    // sprites against a different texture.
    trace("{} sprite(s) for a panel and eight letters", drawn);
    CHECK(drawn > 1);

    const Rectangle box = textbox_rectangle(test_theme(), SCREEN);

    std::size_t marked = 0;

    for (int x = static_cast<int>(box.position.x) + 2;
         x < static_cast<int>(box.right()) - 2; ++x)
    {
        for (int y = static_cast<int>(box.position.y) + 2;
             y < static_cast<int>(box.bottom()) - 2; ++y)
        {
            if (!is_colour(pixel_at_virtual(target, x, y), BOX_RED))
            {
                ++marked;
            }
        }
    }

    trace("{} pixel(s) inside the box are not the panel's colour", marked);
    CHECK(marked > 0);
}

TEST_CASE("a typewriter half way through draws less than the whole line",
          "[present][view][gpu]")
{
    gl_context();

    const RenderTarget target(TARGET_SIZE);
    REQUIRE(target.is_complete());

    Fixture fixture;
    fixture.stage.set_reveal_speed(10.0);
    fixture.stage.say(SayCommand{.text = "ЖЖЖЖЖЖЖЖ"});

    const std::size_t nothing_yet = draw_frame(fixture);

    fixture.stage.advance_reveal(0.4);
    const std::size_t partly = draw_frame(fixture);

    fixture.stage.complete_reveal();
    const std::size_t whole = draw_frame(fixture);

    trace("{} then {} then {} sprite(s)", nothing_yet, partly, whole);

    CHECK(nothing_yet < partly);
    CHECK(partly < whole);
}

TEST_CASE("a menu lights only the choice the reader is on", "[present][view][gpu]")
{
    gl_context();

    const RenderTarget target(TARGET_SIZE);
    REQUIRE(target.is_complete());

    Fixture fixture;
    fixture.stage.offer(MenuCommand{.prompts = {"Да", "Нет"}});

    draw_frame(fixture, 1);

    const auto first = pixel_at_virtual(target, centre_of_choice(0, 2) + glm::vec2{-90.0f, 0.0f});
    const auto second = pixel_at_virtual(target, centre_of_choice(1, 2) + glm::vec2{-90.0f, 0.0f});

    trace_pixel("the choice nobody is on", first);
    trace_pixel("the choice under the pointer", second);

    CHECK(is_colour(first, CHOICE_BLUE));
    CHECK(is_colour(second, CHOICE_GREEN));
}

TEST_CASE("no menu means no panels", "[present][view][gpu]")
{
    gl_context();

    const RenderTarget target(TARGET_SIZE);
    REQUIRE(target.is_complete());

    Fixture fixture;
    fixture.stage.offer(MenuCommand{.prompts = {"Да", "Нет"}});
    fixture.stage.close_menu();

    draw_frame(fixture);

    CHECK(is_colour(pixel_at_virtual(target, centre_of_choice(0, 2)), BLACK));
}

TEST_CASE("a background nobody shipped is drawn as the placeholder",
          "[present][view][gpu]")
{
    gl_context();

    const RenderTarget target(TARGET_SIZE);
    REQUIRE(target.is_complete());

    const LogCaptureGuard capture;

    Fixture fixture;
    fixture.stage.scene(SceneCommand{.background = "nowhere"});

    draw_frame(fixture);

    // Something has to be on screen, and the decision to draw the placeholder
    // belongs to the presentation layer: the manager reports the hole, this fills
    // it, and the story carries on.
    const auto corner = pixel_at_virtual(target, 2, 2);
    trace_pixel("the top-left corner", corner);

    CHECK_FALSE(is_colour(corner, BLACK));
}

TEST_CASE("a missing asset is asked for once, however many frames are drawn",
          "[present][view][gpu]")
{
    gl_context();

    const RenderTarget target(TARGET_SIZE);
    REQUIRE(target.is_complete());

    const LogCaptureGuard capture;

    Fixture fixture;
    fixture.stage.scene(SceneCommand{.background = "nowhere"});

    for (int frame = 0; frame < 5; ++frame)
    {
        draw_frame(fixture);
    }

    // The ledger the Application prints at the end of a run would be worthless if
    // a background drawn sixty times a second filled it sixty times a second.
    trace("{} missing asset(s) recorded over five frames", fixture.assets.missing().size());
    CHECK(fixture.assets.missing().size() == 1);
}

TEST_CASE("a sprite stands on its placement point", "[present][view][gpu]")
{
    gl_context();

    const RenderTarget target(TARGET_SIZE);
    REQUIRE(target.is_complete());

    const LogCaptureGuard capture;

    Fixture fixture;
    fixture.stage.show(ShowCommand{.asset = "alice/happy", .anchor = "left"});

    draw_frame(fixture);

    // The placeholder is 64 by 64, and the placement point is the middle of a
    // sprite's bottom edge, so the picture occupies the square that ends on the
    // floor line at a quarter of the way across.
    const Rectangle area =
        sprite_rectangle(position_of(Anchor::LEFT), glm::vec2{64.0f, 64.0f}, SCREEN);

    trace("the sprite is at ({}, {}), {}x{}", area.position.x, area.position.y, area.size.x,
          area.size.y);

    const auto inside = pixel_at_virtual(target, area.position + area.size * 0.5f);
    const auto beside =
        pixel_at_virtual(target, glm::vec2{area.right() + 20.0f, area.position.y + 30.0f});
    const auto below =
        pixel_at_virtual(target, glm::vec2{area.position.x + 30.0f, area.bottom() - 2.0f});

    trace_pixel("the middle of the sprite", inside);
    trace_pixel("well to the right of it", beside);
    trace_pixel("its last row, on the floor line", below);

    CHECK_FALSE(is_colour(inside, BLACK));
    CHECK(is_colour(beside, BLACK));
    CHECK_FALSE(is_colour(below, BLACK));
}

TEST_CASE("the failure screen covers what was behind it", "[present][view][gpu]")
{
    gl_context();

    const RenderTarget target(TARGET_SIZE);
    REQUIRE(target.is_complete());

    Fixture fixture;

    auto batch = SpriteBatch::create();
    REQUIRE(batch.has_value());

    const Viewport viewport{TARGET_SIZE, TARGET_SIZE};

    cpen::render::clear(glm::vec4{0.0f, 1.0f, 0.0f, 1.0f});

    batch->begin(viewport.projection());
    fixture.view.draw_message(*batch, SCREEN, "script/intro.pen:3:5: error: сломалось");
    batch->end();

    // A diagnostic read against the scene that produced it is a diagnostic
    // misread, so the backdrop is opaque.
    CHECK_FALSE(is_colour(pixel_at_virtual(target, 2, 2), glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}));
    CHECK_FALSE(is_colour(pixel_at_virtual(target, TARGET_SIZE - 3, TARGET_SIZE - 3),
                          glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}));
}
