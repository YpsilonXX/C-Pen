#include "cpen/render/renderer.hh"

#include "cpen/core/log.hh"
#include "cpen/render/draw.hh"

#include <utility>

namespace cpen::render
{
    std::expected<Renderer, core::Error> Renderer::create(const std::uint32_t virtual_width,
                                                          const std::uint32_t virtual_height,
                                                          const ScaleMode mode,
                                                          const std::size_t sprite_capacity)
    {
        auto created = SpriteBatch::create(sprite_capacity);
        if (!created)
        {
            return std::unexpected(created.error());
        }

        return Renderer{Viewport{virtual_width, virtual_height, mode}, std::move(*created)};
    }

    Renderer::Renderer(Viewport initial_viewport)
        : frame_viewport(std::move(initial_viewport))
    {
    }

    Renderer::Renderer(Viewport initial_viewport, SpriteBatch initial_batch)
        : frame_viewport(std::move(initial_viewport)),
          batch(std::move(initial_batch))
    {
    }

    void Renderer::resize(const std::uint32_t framebuffer_width,
                          const std::uint32_t framebuffer_height)
    {
        this->frame_viewport.resize(framebuffer_width, framebuffer_height);

        // The arithmetic above happens either way; only the GL call is conditional.
        // A renderer with no resources makes no GL call, and that is the whole
        // property that lets a state stack be tested without a driver.
        if (this->can_draw())
        {
            set_viewport(this->frame_viewport.rect());
        }
    }

    void Renderer::begin_frame()
    {
        if (this->frame_open)
        {
            if (!this->reported_misuse)
            {
                this->reported_misuse = true;
                log::error(log::Category::RENDER,
                           "renderer: begin_frame() was called with a frame already open; "
                           "the previous one is closed first");
            }
            this->end_frame();
        }

        this->frame_open = true;

        if (!this->can_draw())
        {
            return;
        }

        // The clear covers the whole framebuffer including the letterbox bars,
        // which is what makes them the background's colour rather than whatever the
        // driver left there. The viewport does not restrict it: glClear ignores the
        // viewport rectangle, and scissoring it to the content would leave the bars
        // holding the previous frame.
        clear(this->background);

        this->batch->begin(this->frame_viewport.projection());
    }

    void Renderer::end_frame()
    {
        if (!this->frame_open)
        {
            if (!this->reported_misuse)
            {
                this->reported_misuse = true;
                log::error(log::Category::RENDER,
                           "renderer: end_frame() was called without a matching "
                           "begin_frame()");
            }
            return;
        }

        if (this->can_draw())
        {
            this->batch->end();
        }

        this->frame_open = false;
    }

    SpriteBatch* Renderer::sprites() noexcept
    {
        return this->batch.has_value() ? &*this->batch : nullptr;
    }

    std::size_t Renderer::draw_calls() const noexcept
    {
        return this->batch.has_value() ? this->batch->draw_calls() : 0;
    }

    std::size_t Renderer::sprite_count() const noexcept
    {
        return this->batch.has_value() ? this->batch->sprite_count() : 0;
    }
}
