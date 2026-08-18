#include "cpen/runtime/dialogue_state.hh"

#include "cpen/core/log.hh"
#include "cpen/render/renderer.hh"
#include "cpen/render/sprite_batch.hh"
#include "cpen/runtime/game_context.hh"
#include "cpen/runtime/state_stack.hh"
#include "cpen/script/diagnostic.hh"

#include <utility>
#include <variant>

namespace cpen::runtime
{
    namespace
    {
        /// The choice a number key asks for, or nothing if the key is not one.
        ///
        /// Both rows are accepted: a keypad digit is the same digit, and refusing
        /// it would be a rule nobody could guess. Zero is left out because the
        /// choices are numbered from one on screen.
        std::optional<std::size_t> digit_choice(const platform::Key key) noexcept
        {
            const auto code = static_cast<int>(key);

            if (key >= platform::Key::DIGIT_1 && key <= platform::Key::DIGIT_9)
            {
                return static_cast<std::size_t>(code - static_cast<int>(platform::Key::DIGIT_1));
            }

            if (key >= platform::Key::KEYPAD_1 && key <= platform::Key::KEYPAD_9)
            {
                return static_cast<std::size_t>(code - static_cast<int>(platform::Key::KEYPAD_1));
            }

            return std::nullopt;
        }
    }

    DialogueState::DialogueState(Config settings)
        : configuration(std::move(settings))
    {
    }

    void DialogueState::on_enter()
    {
        this->current_stage.set_reveal_speed(this->configuration.reveal_speed);

        this->view.emplace(this->context().assets, this->configuration.theme);

        // Typefaces and textures are GL objects, and there is no context to make
        // them in when the renderer never got one. Skipping the load is what lets
        // a whole story be played through in a test: everything below is unchanged
        // and render() simply has nothing to draw with.
        if (this->context().renderer.can_draw())
        {
            this->view->load();
        }

        std::expected<assets::ScriptReference, core::Error> loaded =
            this->context().assets.script(this->configuration.script);

        if (!loaded.has_value())
        {
            // The message is the compiler's own rendered diagnostics when the file
            // was found and would not compile, and the search that failed when it
            // was not. Both are worth a reader's eyes, which is why neither is only
            // logged.
            this->fail(loaded.error().message);
            return;
        }

        this->program = std::move(*loaded);

        this->virtual_machine.emplace(this->program->chunk, this->context().blackboard,
                                      this->current_stage);

        if (this->configuration.label.empty())
        {
            this->virtual_machine->start();
        }
        else if (!this->virtual_machine->start_at_label(this->configuration.label))
        {
            this->fail(std::format("{}: there is no label '{}' in this script",
                                   this->program->name, this->configuration.label));
            return;
        }

        log::info(log::Category::PRESENT, "playing '{}'{}", this->program->name,
                  this->configuration.label.empty()
                      ? std::string{}
                      : std::format(" from '{}'", this->configuration.label));

        this->pump();
    }

    void DialogueState::on_exit()
    {
        // Released here rather than in the destructor, because the stack calls
        // on_exit() while the GL context is still current: the Application
        // declares the stack after the window, so every state is taken down before
        // the context it made its objects in.
        if (this->view.has_value())
        {
            this->view->release();
        }
    }

    void DialogueState::pump()
    {
        if (!this->virtual_machine.has_value())
        {
            return;
        }

        const script::YieldStatus status = this->virtual_machine->run();

        switch (status)
        {
            case script::YieldStatus::WAITING_FOR_TIME:
                this->remaining_wait = this->virtual_machine->wait_duration();
                return;

            case script::YieldStatus::WAITING_FOR_CHOICE:
                // Nothing is highlighted until the reader says so, by moving the
                // pointer onto a choice or by pressing a key: a menu that opens
                // with an answer already picked out invites taking it by accident.
                this->highlighted.reset();
                return;

            case script::YieldStatus::WAITING_FOR_ADVANCE:
            case script::YieldStatus::RUNNING:
                return;

            case script::YieldStatus::FINISHED:
                log::info(log::Category::PRESENT, "'{}' ended", this->program->name);

                // Popping is how a state ends, and popping the last one empties the
                // stack, which ends the loop. A story that runs out needs no
                // separate channel back to the application.
                this->stack().pop();
                return;

            case script::YieldStatus::FAULTED:
            {
                const std::optional<script::RuntimeFault>& fault =
                    this->virtual_machine->fault();

                if (!fault.has_value())
                {
                    this->fail("the script faulted without saying why");
                    return;
                }

                // Rendered as a diagnostic although it is not one: nothing is
                // wrong with the file as written, but the only useful thing to
                // show an author is the line that was executing, with a caret
                // under it — which is exactly what a diagnostic is.
                this->fail(script::render_diagnostic(
                    script::Diagnostic{
                        .severity = script::Severity::ERROR,
                        .span = fault->span,
                        .message = fault->message,
                    },
                    this->program->source, this->program->name));
                return;
            }
        }
    }

    bool DialogueState::advance()
    {
        if (!this->failure_message.empty() || !this->virtual_machine.has_value())
        {
            return false;
        }

        // The first answer to a click is the rest of the line, the second is the
        // next one. A reader who has read faster than the typewriter should never
        // have to wait for it.
        if (!this->current_stage.reveal_complete())
        {
            this->current_stage.complete_reveal();
            return true;
        }

        if (this->virtual_machine->status() != script::YieldStatus::WAITING_FOR_ADVANCE)
        {
            return false;
        }

        if (!this->virtual_machine->resume(script::AdvanceEvent{}))
        {
            return false;
        }

        this->pump();
        return true;
    }

    bool DialogueState::choose(const std::size_t index)
    {
        if (!this->virtual_machine.has_value() ||
            this->virtual_machine->status() != script::YieldStatus::WAITING_FOR_CHOICE)
        {
            return false;
        }

        if (index >= this->virtual_machine->choice_count())
        {
            return false;
        }

        if (!this->virtual_machine->resume(script::ChoiceEvent{
                .index = static_cast<std::uint32_t>(index)}))
        {
            return false;
        }

        // Taken down only once the machine has accepted the answer, so a refused
        // choice leaves the menu exactly as it was.
        this->current_stage.close_menu();
        this->highlighted.reset();

        this->pump();
        return true;
    }

    void DialogueState::move_highlight(const int step)
    {
        const std::size_t count = this->current_stage.choices().size();

        if (count == 0)
        {
            return;
        }

        const auto total = static_cast<int>(count);

        // Wrapping at both ends, and starting from the last choice when the
        // highlight moves up out of nothing, so that one press of either arrow
        // lands on the end of the list nearest the direction pressed.
        const int current = this->highlighted.has_value()
                                ? static_cast<int>(*this->highlighted)
                                : (step > 0 ? -1 : 0);

        const int moved = ((current + step) % total + total) % total;

        this->highlighted = static_cast<std::size_t>(moved);
    }

    bool DialogueState::handle_key(const platform::KeyEvent& key)
    {
        if (key.action == platform::InputAction::RELEASE)
        {
            return false;
        }

        const bool choosing = this->current_stage.has_menu();

        if (choosing)
        {
            if (const std::optional<std::size_t> chosen = digit_choice(key.key))
            {
                return this->choose(*chosen);
            }

            if (key.key == platform::Key::DOWN)
            {
                this->move_highlight(1);
                return true;
            }

            if (key.key == platform::Key::UP)
            {
                this->move_highlight(-1);
                return true;
            }

            if (key.key == platform::Key::ENTER || key.key == platform::Key::SPACE)
            {
                // Only a highlighted choice can be confirmed: pressing space at a
                // menu nobody has moved through must not answer it at random.
                return this->highlighted.has_value() && this->choose(*this->highlighted);
            }

            return false;
        }

        if (key.key == platform::Key::SPACE || key.key == platform::Key::ENTER)
        {
            return this->advance();
        }

        return false;
    }

    bool DialogueState::handle_click(const platform::MouseButtonEvent& click)
    {
        if (click.button != platform::MouseButton::LEFT ||
            click.action != platform::InputAction::PRESS)
        {
            return false;
        }

        if (!this->current_stage.has_menu())
        {
            return this->advance();
        }

        const std::optional<std::size_t> hit = present::choice_at(
            this->configuration.theme, this->screen(),
            this->current_stage.choices().size(), this->to_virtual(click.x, click.y));

        // A click anywhere else is swallowed rather than passed on: a menu is a
        // question, and clicking beside it is not an answer to anything else
        // either.
        return hit.has_value() && this->choose(*hit);
    }

    bool DialogueState::handle_event(const platform::Event& event)
    {
        if (const auto* key = std::get_if<platform::KeyEvent>(&event))
        {
            return this->handle_key(*key);
        }

        if (const auto* click = std::get_if<platform::MouseButtonEvent>(&event))
        {
            return this->handle_click(*click);
        }

        if (const auto* moved = std::get_if<platform::MouseMoveEvent>(&event))
        {
            if (this->current_stage.has_menu())
            {
                // The highlight follows the pointer and is computed from the same
                // function the click will use, so what is lit and what would be
                // taken cannot disagree.
                this->highlighted = present::choice_at(
                    this->configuration.theme, this->screen(),
                    this->current_stage.choices().size(),
                    this->to_virtual(moved->x, moved->y));
            }

            // Not consumed: moving a pointer is not an answer, and a state
            // underneath may legitimately care where it is.
            return false;
        }

        return false;
    }

    void DialogueState::update(const double delta_time)
    {
        if (!this->failure_message.empty())
        {
            return;
        }

        this->current_stage.advance_reveal(delta_time);

        if (!this->virtual_machine.has_value() ||
            this->virtual_machine->status() != script::YieldStatus::WAITING_FOR_TIME)
        {
            return;
        }

        this->remaining_wait -= delta_time;

        if (this->remaining_wait > 0.0)
        {
            return;
        }

        this->remaining_wait = 0.0;

        if (this->virtual_machine->resume(script::TimeElapsedEvent{}))
        {
            this->pump();
        }
    }

    void DialogueState::render()
    {
        render::SpriteBatch* const batch = this->context().renderer.sprites();

        if (batch == nullptr || !this->view.has_value())
        {
            return;
        }

        if (!this->failure_message.empty())
        {
            this->view->draw_message(*batch, this->screen(), this->failure_message);
            return;
        }

        this->view->draw(*batch, this->current_stage, this->screen(), this->highlighted);
    }

    void DialogueState::fail(std::string text)
    {
        log::error(log::Category::PRESENT, "{}", text);

        this->failure_message = std::move(text);

        // Whatever was on stage belongs to a story that is no longer running, and
        // leaving it under the message would suggest it still is.
        this->current_stage.clear();
        this->highlighted.reset();
    }

    glm::vec2 DialogueState::screen() const
    {
        const glm::uvec2 size = this->context().renderer.viewport().virtual_size();

        return glm::vec2{static_cast<float>(size.x), static_cast<float>(size.y)};
    }

    glm::vec2 DialogueState::to_virtual(const double x, const double y) const
    {
        return this->context().renderer.viewport().to_virtual(
            glm::vec2{static_cast<float>(x), static_cast<float>(y)});
    }
}
