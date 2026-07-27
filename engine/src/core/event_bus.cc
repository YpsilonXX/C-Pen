#include "cpen/core/event_bus.hh"

#include "cpen/core/log.hh"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace cpen::core
{
    namespace detail
    {
        /// Everything the bus owns. Held through a shared_ptr so that a
        /// Subscription can hold a weak reference and detect a bus that is gone.
        struct EventBusState
        {
            struct Handler
            {
                SubscriptionId id = 0;
                std::function<void(const void*)> invoke;

                /// Cleared by unsubscribe. Removal is deferred while a dispatch
                /// pass is running, because the pass indexes into this vector.
                bool active = true;
            };

            struct Channel
            {
                std::vector<Handler> handlers;
            };

            /// unordered_map is required here, not merely convenient: dispatch
            /// holds a reference into a channel while handlers run, and those
            /// handlers may subscribe to further event types. Only a node-based
            /// container guarantees the held reference survives that insertion.
            std::unordered_map<std::type_index, Channel> channels;

            std::vector<std::unique_ptr<QueuedEvent>> queue;
            SubscriptionId next_id = 1;
            unsigned dispatch_depth = 0;

            void deactivate(const std::type_index event_type, const SubscriptionId id)
            {
                const auto channel = this->channels.find(event_type);
                if (channel == this->channels.end())
                {
                    return;
                }

                auto& handlers = channel->second.handlers;
                const auto handler = std::ranges::find(handlers, id, &Handler::id);
                if (handler == handlers.end())
                {
                    return;
                }

                if (this->dispatch_depth > 0)
                {
                    handler->active = false;
                    return;
                }
                handlers.erase(handler);
            }

            void compact()
            {
                for (auto& [event_type, channel] : this->channels)
                {
                    std::erase_if(channel.handlers,
                                  [](const Handler& handler) { return !handler.active; });
                }
            }
        };
    }

    Subscription::Subscription() noexcept = default;

    Subscription::Subscription(std::weak_ptr<detail::EventBusState> bus_state,
                               const std::type_index type,
                               const SubscriptionId assigned_id) noexcept
        : owner(std::move(bus_state)), event_type(type), id(assigned_id)
    {
    }

    Subscription::Subscription(Subscription&& other) noexcept
        : owner(std::move(other.owner)),
          event_type(other.event_type),
          id(other.id)
    {
        // A moved-from weak_ptr is empty, so the source can no longer unsubscribe;
        // clearing the identifier keeps active() honest as well.
        other.id = 0;
    }

    Subscription& Subscription::operator=(Subscription&& other) noexcept
    {
        if (this != &other)
        {
            this->release();
            this->owner = std::move(other.owner);
            this->event_type = other.event_type;
            this->id = other.id;
            other.id = 0;
        }
        return *this;
    }

    Subscription::~Subscription()
    {
        this->release();
    }

    bool Subscription::active() const noexcept
    {
        return this->id != 0 && !this->owner.expired();
    }

    void Subscription::release() noexcept
    {
        if (this->id == 0)
        {
            return;
        }

        if (const auto owning_state = this->owner.lock())
        {
            owning_state->deactivate(this->event_type, this->id);
        }

        this->owner.reset();
        this->id = 0;
    }

    EventBus::EventBus()
        : state(std::make_shared<detail::EventBusState>())
    {
    }

    EventBus::~EventBus() = default;

    Subscription EventBus::add_handler(const std::type_index event_type,
                                       std::function<void(const void*)> handler)
    {
        const SubscriptionId id = this->state->next_id++;
        this->state->channels[event_type].handlers.push_back(
            detail::EventBusState::Handler{.id = id, .invoke = std::move(handler), .active = true});
        return Subscription(this->state, event_type, id);
    }

    void EventBus::enqueue(std::unique_ptr<detail::QueuedEvent> event)
    {
        this->state->queue.push_back(std::move(event));
    }

    void EventBus::dispatch_pending()
    {
        if (this->state->dispatch_depth > 0)
        {
            log::error(log::Category::CORE,
                       "dispatch_pending() called from inside a handler; ignored");
            return;
        }

        for (unsigned pass = 0; !this->state->queue.empty(); ++pass)
        {
            if (pass == MAX_DISPATCH_PASSES)
            {
                // Left in the queue rather than dropped: the next frame either
                // clears the backlog or reports again, and nothing is lost silently.
                log::error(log::Category::CORE,
                           "event dispatch did not settle after {} passes, {} event(s) deferred",
                           MAX_DISPATCH_PASSES, this->state->queue.size());
                return;
            }

            // Detaching the batch is what makes generations work: anything a
            // handler publishes lands in a queue nobody is iterating.
            std::vector<std::unique_ptr<detail::QueuedEvent>> batch;
            batch.swap(this->state->queue);

            ++this->state->dispatch_depth;
            for (const auto& event : batch)
            {
                const auto channel = this->state->channels.find(event->type());
                if (channel == this->state->channels.end())
                {
                    continue;
                }

                auto& handlers = channel->second.handlers;

                // Snapshot the count so a handler that subscribes to this same
                // event type does not receive the event it is currently handling.
                // Indexing rather than iterating survives the reallocation such a
                // subscription may cause.
                const std::size_t count = handlers.size();
                for (std::size_t i = 0; i < count; ++i)
                {
                    if (handlers[i].active)
                    {
                        handlers[i].invoke(event->payload());
                    }
                }
            }
            --this->state->dispatch_depth;

            this->state->compact();
        }
    }

    std::size_t EventBus::pending_count() const noexcept
    {
        return this->state->queue.size();
    }

    std::size_t EventBus::subscriber_count() const noexcept
    {
        std::size_t total = 0;
        for (const auto& [event_type, channel] : this->state->channels)
        {
            total += static_cast<std::size_t>(std::ranges::count(
                channel.handlers, true, &detail::EventBusState::Handler::active));
        }
        return total;
    }

    std::size_t EventBus::subscriber_count(const std::type_index event_type) const noexcept
    {
        const auto channel = this->state->channels.find(event_type);
        if (channel == this->state->channels.end())
        {
            return 0;
        }
        return static_cast<std::size_t>(std::ranges::count(
            channel->second.handlers, true, &detail::EventBusState::Handler::active));
    }

    void EventBus::clear_pending()
    {
        this->state->queue.clear();
    }

    void EventBus::clear_subscribers()
    {
        for (auto& [event_type, channel] : this->state->channels)
        {
            for (auto& handler : channel.handlers)
            {
                handler.active = false;
            }
        }

        if (this->state->dispatch_depth == 0)
        {
            this->state->compact();
        }
    }
}
