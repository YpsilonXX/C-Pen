#include "cpen/app/application.hh"

#include "cpen/core/log.hh"

#include <algorithm>
#include <utility>

namespace cpen::app
{
    Application::Application()
        : Application(Config{})
    {
    }

    Application::Application(Config settings)
        : configuration(std::move(settings)),
          main_window(this->platform_context, this->configuration.window),
          blackboard(this->event_bus),
          game_context(runtime::GameContext{
              .blackboard = this->blackboard,
              .event_bus = this->event_bus,
          }),
          stack(this->game_context)
    {
        log::info(log::Category::APP, "application initialised, window {}x{}",
                  this->configuration.window.width, this->configuration.window.height);
    }

    void Application::route_events()
    {
        for (const platform::Event& event : this->main_window.events())
        {
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
