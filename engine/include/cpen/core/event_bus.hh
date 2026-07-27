#ifndef CPEN_CORE_EVENT_BUS_HH
#define CPEN_CORE_EVENT_BUS_HH

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <utility>

namespace cpen::core
{
    /// Identifies one subscription within its event type. Never reused within a
    /// bus, so a stale identifier can never resolve to a later handler.
    using SubscriptionId = std::uint64_t;

    namespace detail
    {
        struct EventBusState;

        /// Type-erased carrier for a queued event. Deferred delivery means the
        /// event must outlive the publish call, so it is stored by value behind
        /// this interface; the handler wrapper knows the concrete type and casts
        /// `payload()` back.
        class QueuedEvent
        {
        public:
            virtual ~QueuedEvent() = default;

            QueuedEvent() = default;
            QueuedEvent(const QueuedEvent&) = delete;
            QueuedEvent& operator=(const QueuedEvent&) = delete;

            virtual std::type_index type() const noexcept = 0;
            virtual const void* payload() const noexcept = 0;
        };

        template <typename EventType>
        class TypedEvent final : public QueuedEvent
        {
        public:
            explicit TypedEvent(EventType value)
                : event(std::move(value))
            {
            }

            std::type_index type() const noexcept override
            {
                return std::type_index(typeid(EventType));
            }

            const void* payload() const noexcept override
            {
                return &this->event;
            }

        private:
            EventType event;
        };
    }

    /// Move-only handle that owns a subscription: letting it go unsubscribes.
    ///
    /// The bus is referenced weakly, so a handle outliving its bus is harmless
    /// rather than a dangling write. Releasing during dispatch is safe: the
    /// handler is deactivated immediately and erased once the pass ends.
    class Subscription
    {
    public:
        Subscription() noexcept;
        ~Subscription();

        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;

        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;

        /// True while this handle still refers to a live subscription on a live
        /// bus.
        bool active() const noexcept;

        /// Unsubscribes now instead of at destruction. Idempotent.
        void release() noexcept;

    private:
        friend class EventBus;

        Subscription(std::weak_ptr<detail::EventBusState> bus_state,
                     std::type_index type,
                     SubscriptionId assigned_id) noexcept;

        std::weak_ptr<detail::EventBusState> owner{};
        std::type_index event_type{typeid(void)};
        SubscriptionId id = 0;
    };

    /// Engine-wide publish/subscribe channel with deferred delivery.
    ///
    /// Events are identified by their C++ type, so any layer declares its own
    /// event structs and core never learns about them. Script-authored signals
    /// are expected to travel as one such type carrying a name and arguments,
    /// rather than as a second mechanism.
    ///
    /// Handlers never run inside publish(): a script handler invoked from the
    /// middle of a VM step would re-enter the VM, and the frame's event order
    /// would depend on where each publish happened to sit. Instead events queue
    /// up and the application drains them at one fixed point per frame, which is
    /// what keeps the loop deterministic and save states reproducible.
    ///
    /// Single-threaded by design, in line with the deterministic core loop:
    /// cross-thread results arrive through their own queue, not through this bus.
    class EventBus
    {
    public:
        /// Upper bound on generations processed by one dispatch_pending() call.
        /// Handlers that publish keep the drain going; a handler cycle that never
        /// settles is reported instead of hanging the frame.
        static constexpr unsigned MAX_DISPATCH_PASSES = 8;

        EventBus();
        ~EventBus();

        EventBus(const EventBus&) = delete;
        EventBus& operator=(const EventBus&) = delete;

        /// Registers `handler` for events of type EventType. Handlers of one type
        /// are invoked in subscription order.
        template <typename EventType>
        [[nodiscard]] Subscription subscribe(std::function<void(const EventType&)> handler)
        {
            return this->add_handler(
                std::type_index(typeid(EventType)),
                [callable = std::move(handler)](const void* payload)
                {
                    callable(*static_cast<const EventType*>(payload));
                });
        }

        /// Enqueues a copy of `event` for delivery at the next dispatch_pending().
        /// Publishing from inside a handler is allowed; the event is delivered in
        /// the following generation, never re-entrantly.
        template <typename EventType>
        void publish(EventType event)
        {
            this->enqueue(std::make_unique<detail::TypedEvent<EventType>>(std::move(event)));
        }

        /// Drains the queue, repeating while handlers publish further events.
        /// Called once per frame at the loop's synchronisation point. Recursive
        /// calls from within a handler are rejected and logged.
        void dispatch_pending();

        std::size_t pending_count() const noexcept;
        std::size_t subscriber_count() const noexcept;
        std::size_t subscriber_count(std::type_index event_type) const noexcept;

        /// Drops queued events without delivering them; used when a state stack is
        /// torn down mid-frame (load, quit) and the events no longer apply.
        void clear_pending();

        /// Deactivates every handler. Outstanding Subscription handles stay valid
        /// and simply become inert.
        void clear_subscribers();

    private:
        Subscription add_handler(std::type_index event_type,
                                 std::function<void(const void*)> handler);
        void enqueue(std::unique_ptr<detail::QueuedEvent> event);

        std::shared_ptr<detail::EventBusState> state;
    };
}

#endif //CPEN_CORE_EVENT_BUS_HH
