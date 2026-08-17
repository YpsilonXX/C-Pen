#include "cpen/app/application.hh"

#include "cpen/core/log.hh"
#include "cpen/render/draw.hh"

#include <algorithm>
#include <utility>
#include <variant>

namespace cpen::app
{
    Application::Application()
        : Application(Config{})
    {
    }

    Application::Application(Config settings)
        : configuration(std::move(settings)),
          main_window(this->platform_context, this->configuration.window),
          main_viewport(this->configuration.virtual_width, this->configuration.virtual_height,
                        this->configuration.scale_mode),
          blackboard(this->event_bus),
          game_context(runtime::GameContext{
              .blackboard = this->blackboard,
              .event_bus = this->event_bus,
              .viewport = this->main_viewport,
          }),
          stack(this->game_context)
    {
        this->refit_viewport();

        log::info(log::Category::APP,
                  "application initialised, window {}x{}, virtual {}x{} ({})",
                  this->configuration.window.width, this->configuration.window.height,
                  this->main_viewport.virtual_size().x, this->main_viewport.virtual_size().y,
                  render::to_string(this->main_viewport.scale_mode()));
    }

    void Application::refit_viewport()
    {
        const platform::Size framebuffer = this->main_window.framebuffer_size();
        this->main_viewport.resize(framebuffer.width, framebuffer.height);
        render::set_viewport(this->main_viewport.rect());
    }

    void Application::route_events()
    {
        for (const platform::Event& event : this->main_window.events())
        {
            // The viewport is refitted before the states see the resize, not after
            // and not instead: it is engine state that a state may legitimately
            // want to read while reacting — a layout recomputing itself wants the
            // new mapping, not the previous frame's — and the event is still passed
            // on afterwards, since this is a notification rather than a claim on it.
            // The new size is read back from the window rather than taken from the
            // event, so that startup and resize refit through one path.
            if (std::holds_alternative<platform::ResizeEvent>(event))
            {
                this->refit_viewport();
            }

            this->stack.handle_event(event);
        }
    }

    void Application::run()
    {
        // The initial state is queued like any other, so it enters through exactly
        // the same path as every later transition.
        this->stack.apply_pending();

        if (this->stack.empty())
        {
            log::error(log::Category::APP, "run() called with an empty state stack");
            return;
        }

        double previous_time = this->platform_context.time();

        while (this->running && !this->main_window.should_close() && !this->stack.empty())
        {
            const double now = this->platform_context.time();
            const double delta_time =
                std::min(now - previous_time, this->configuration.maximum_delta_time);
            previous_time = now;

            this->platform_context.poll_events();

            // The fixed order of a frame. Events reach the states first, so update
            // already sees this frame's input. The bus is drained after update, so
            // everything the states published during the frame is delivered in one
            // place. Structural changes land after that, which lets an event
            // handler request a transition and still have it take effect before
            // anything is drawn. Rendering therefore always sees a settled stack.
            this->route_events();
            this->stack.update(delta_time);
            this->event_bus.dispatch_pending();
            this->stack.apply_pending();

            this->stack.render();
            this->main_window.swap_buffers();
        }

        log::info(log::Category::APP, "loop ended: running={}, window closing={}, states={}",
                  this->running, this->main_window.should_close(), this->stack.size());
    }
}
