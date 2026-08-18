#ifndef CPEN_PRESENT_STAGE_HH
#define CPEN_PRESENT_STAGE_HH

#include "cpen/script/command_sink.hh"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cpen::present
{
    /// A place on the stage a script can name instead of computing a coordinate.
    ///
    /// The engine's vocabulary, not the language's: the compiler passes the word
    /// through untouched, so adding a name here does not change the grammar and
    /// does not invalidate a script that never uses it. The two off-screen names
    /// exist for the same reason the far ones do — a sprite that enters from the
    /// side has to be somewhere before it enters, and that place is expressible
    /// today even though nothing animates towards it until transitions land.
    enum class Anchor : std::uint8_t
    {
        OFFSCREEN_LEFT,
        FAR_LEFT,
        LEFT,
        CENTER,
        RIGHT,
        FAR_RIGHT,
        OFFSCREEN_RIGHT,
    };

    /// The name the engine prints for the anchor, which is also the spelling an
    /// author writes.
    std::string_view to_string(Anchor anchor) noexcept;

    /// The anchor a written word means, or nothing if the engine has no such
    /// place. "centre" is accepted alongside "center": the two spellings are the
    /// same word, and refusing one of them teaches nothing.
    std::optional<Anchor> anchor_from_name(std::string_view name) noexcept;

    /// Where the anchor puts a sprite's placement point, in the normalised
    /// coordinates the language uses.
    ///
    /// The vertical is the bottom of the reference screen for every anchor,
    /// because a placement point is the middle of a sprite's bottom edge: a
    /// character stands on the floor line, and characters of different heights
    /// stand on the same one without the script saying so.
    script::ScreenPosition position_of(Anchor anchor) noexcept;

    /// The layer an asset name occupies: everything before the first '/'.
    ///
    /// The counterpart of the compiler's rule that joins the words of
    /// `show alice happy` into "alice/happy". It is what makes `hide alice`
    /// find the sprite, and what makes `show alice sad` replace `show alice happy`
    /// rather than stand a second Alice beside the first.
    std::string_view layer_of(std::string_view asset) noexcept;

    /// One sprite standing on the stage.
    ///
    /// Placement is kept as the script wrote it rather than as a resolved point:
    /// an anchor is a name the presentation layer is free to reinterpret when a
    /// layout changes, and a record that had already collapsed it into a number
    /// could not be reinterpreted at all.
    struct StageSprite
    {
        std::string layer{};
        std::string asset{};
        std::string anchor{};
        std::optional<script::ScreenPosition> position{};
        std::string transition{};
    };

    /// Where the sprite actually stands: its coordinates if it was given any, the
    /// anchor's place otherwise, and the middle of the stage if the anchor is a
    /// word this engine does not know.
    script::ScreenPosition placement_of(const StageSprite& sprite) noexcept;

    /// What is on screen, as a value.
    ///
    /// The receiving end of everything the machine has to say, and deliberately
    /// nothing more: it holds a background, the sprites standing on it, the line
    /// being spoken and the choices on offer, and it draws none of them. A test
    /// asks it what the stage looks like without a window existing, and the class
    /// that does draw reads exactly the same fields.
    ///
    /// The typewriter lives here rather than in the drawing code because how much
    /// of a line has been said is part of what the stage *is*: the reader's first
    /// click finishes the line and the second goes on, and that rule is about the
    /// state of the story, not about how the glyphs are put on screen.
    class Stage final : public script::CommandSink
    {
    public:
        /// Code points a line reveals per second. Zero or less puts the whole line
        /// up at once, which is both a legitimate taste and what a test wants.
        static constexpr double DEFAULT_REVEAL_SPEED = 45.0;

        Stage() = default;

        void say(const script::SayCommand& command) override;
        void offer(const script::MenuCommand& command) override;
        void scene(const script::SceneCommand& command) override;
        void show(const script::ShowCommand& command) override;
        void hide(const script::HideCommand& command) override;

        /// Advances the typewriter. Harmless when there is no line and when the
        /// line is already complete.
        void advance_reveal(double delta_time) noexcept;

        /// Puts the rest of the line up at once, which is what the reader's first
        /// click asks for.
        void complete_reveal() noexcept;

        bool reveal_complete() const noexcept;

        /// The part of the line that has been typed out so far. A prefix of the
        /// whole line, cut at a code point boundary so a multi-byte character
        /// never appears half-written.
        std::string_view revealed_text() const noexcept;

        void set_reveal_speed(double code_points_per_second) noexcept
        {
            this->speed = code_points_per_second;
        }

        double reveal_speed() const noexcept { return this->speed; }

        const std::string& background() const noexcept { return this->current_background; }

        /// The transition the background was last changed with. Nothing acts on it
        /// yet; it is carried so that the phase which animates transitions has the
        /// name the author wrote rather than having to re-run the script.
        const std::string& background_transition() const noexcept
        {
            return this->current_background_transition;
        }

        /// The sprites in the order they were first shown, which is the order they
        /// are drawn in. Changing a sprite's picture keeps its place in that order:
        /// a character who changes expression must not jump in front of the one
        /// standing beside her.
        std::span<const StageSprite> sprites() const noexcept { return this->stage_sprites; }

        /// The sprite on that layer, or nullptr.
        const StageSprite* sprite(std::string_view layer) const noexcept;

        /// The line being spoken, whole. Absent before the script says anything.
        const std::optional<script::SayCommand>& line() const noexcept
        {
            return this->current_line;
        }

        bool has_menu() const noexcept { return this->menu_open; }
        std::span<const std::string> choices() const noexcept { return this->current_choices; }

        /// Takes the menu down. Called when the choice has been made, since the
        /// machine is told the answer through its own resume and never through
        /// this class.
        void close_menu() noexcept;

        /// Empties the stage: no background, no sprites, no line, no menu.
        void clear() noexcept;

    private:
        /// Replaces the line and restarts the typewriter.
        void begin_line();

        std::string current_background{};
        std::string current_background_transition{};

        std::vector<StageSprite> stage_sprites{};

        std::optional<script::SayCommand> current_line{};

        /// Byte offset of every code point boundary of the current line, the end
        /// of the string included, so the first `n` code points are the first
        /// `boundaries[n]` bytes.
        ///
        /// Computed once per line rather than by walking the text every frame:
        /// the typewriter runs sixty times a second over a string whose length
        /// never changes, and re-decoding it each time is work with no result.
        std::vector<std::size_t> boundaries{};

        /// Code points revealed so far, fractional because a reveal speed is a
        /// rate and a frame is not a whole number of characters.
        double revealed = 0.0;

        double speed = DEFAULT_REVEAL_SPEED;

        std::vector<std::string> current_choices{};
        bool menu_open = false;
    };
}

#endif //CPEN_PRESENT_STAGE_HH
