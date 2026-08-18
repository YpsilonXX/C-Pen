#include "cpen/app/application.hh"

#include "cpen/core/log.hh"

#include <algorithm>
#include <expected>
#include <optional>
#include <string>
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
          main_renderer(make_renderer(this->configuration)),
          blackboard(this->event_bus),
          asset_resolver(this->file_system),
          asset_manager(this->file_system, this->asset_resolver),
          game_context(runtime::GameContext{
              .blackboard = this->blackboard,
              .event_bus = this->event_bus,
              .renderer = this->main_renderer,
              .assets = this->asset_manager,
          }),
          stack(this->game_context)
    {
        this->mount_roots();
        this->refit_viewport();

        const render::Viewport& viewport = this->main_renderer.viewport();

        log::info(log::Category::APP,
                  "application initialised, window {}x{}, virtual {}x{} ({}), drawing {}",
                  this->configuration.window.width, this->configuration.window.height,
                  viewport.virtual_size().x, viewport.virtual_size().y,
                  render::to_string(viewport.scale_mode()),
                  this->main_renderer.can_draw() ? "enabled" : "unavailable");
    }

    render::Renderer Application::make_renderer(const Config& settings)
    {
        auto created = render::Renderer::create(settings.virtual_width, settings.virtual_height,
                                                settings.scale_mode, settings.sprite_capacity);
        if (created)
        {
            return std::move(*created);
        }

        log::error(log::Category::RENDER, "{}", created.error());
        log::error(log::Category::RENDER,
                   "the application continues without the means to draw; the window will "
                   "stay up so that this message can be read");

        return render::Renderer{render::Viewport{settings.virtual_width,
                                                 settings.virtual_height,
                                                 settings.scale_mode}};
    }

    void Application::mount_roots()
    {
        std::optional<AssetRoots> roots = this->configuration.roots;

        if (!roots.has_value())
        {
            std::expected<AssetRoots, core::Error> located = default_asset_roots();

            if (!located.has_value())
            {
                // Survivable in the same way a renderer that will not build is:
                // every asset will report itself missing, each with its own line,
                // which is a far more useful state to be in than a process that
                // exited before anything could be read.
                log::error(log::Category::ASSETS,
                           "{}; no roots are mounted and no asset can be found",
                           located.error());
                return;
            }

            roots = std::move(*located);
        }

        // The game first. Everything the engine ships is a default, and a default
        // that cannot be replaced is not one.
        this->file_system.mount(roots->game);
        this->file_system.mount(roots->engine);
    }

    void Application::report_asset_problems() const
    {
        const std::string missing = this->asset_manager.format_missing_summary();

        if (!missing.empty())
        {
            log::error(log::Category::ASSETS, "{}", missing);
        }

        const std::string mismatched = this->file_system.format_case_mismatch_summary();

        if (!mismatched.empty())
        {
            log::error(log::Category::ASSETS, "{}", mismatched);
        }
    }

    void Application::refit_viewport()
    {
        const platform::Size framebuffer = this->main_window.framebuffer_size();
        this->main_renderer.resize(framebuffer.width, framebuffer.height);
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

            // The frame is opened here and nowhere else. Every state the stack
            // renders, from the deepest visible one upwards, submits into the same
            // batch — so an overlay merges its runs with what it covers instead of
            // starting its own, and no state can leave a frame open.
            this->main_renderer.begin_frame();
            this->stack.render();
            this->main_renderer.end_frame();

            this->main_window.swap_buffers();
        }

        log::info(log::Category::APP, "loop ended: running={}, window closing={}, states={}",
                  this->running, this->main_window.should_close(), this->stack.size());

        this->report_asset_problems();
    }
}
