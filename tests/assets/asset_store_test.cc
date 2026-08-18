#include <catch2/catch_test_macros.hpp>

#include "cpen/assets/asset_store.hh"
#include "cpen/core/error.hh"
#include "cpen/core/log.hh"
#include "support/log_capture.hh"
#include "support/trace.hh"

#include <expected>
#include <format>
#include <string>
#include <utility>

using cpen::assets::AssetHandle;
using cpen::assets::AssetReference;
using cpen::assets::AssetStore;
using cpen::core::Error;
using cpen::core::ErrorCode;
using cpen::core::make_error;
using cpen::test::LogCaptureGuard;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    /// Stands in for a Texture: move-only, and loudly aware of its own lifetime,
    /// so a test can assert that unloading really unloads rather than merely
    /// forgetting.
    struct Counted
    {
        static inline int alive = 0;

        std::string content;

        explicit Counted(std::string text) : content(std::move(text)) { ++Counted::alive; }

        Counted(Counted&& other) noexcept : content(std::move(other.content))
        {
            ++Counted::alive;
        }

        Counted(const Counted&) = delete;
        Counted& operator=(const Counted&) = delete;
        Counted& operator=(Counted&&) = delete;

        ~Counted() { --Counted::alive; }
    };

    /// A loader that reports how often it actually ran.
    struct Loads
    {
        int count = 0;

        auto operator()(const std::string& content)
        {
            return [this, content]() -> std::expected<Counted, Error>
            {
                ++this->count;
                return Counted{content};
            };
        }
    };
}

TEST_CASE("the second request for an asset does not load it again", "[assets][store]")
{
    AssetStore<Counted> store;
    Loads loads;

    const auto first = store.acquire("bg/room", loads("pixels"));
    const auto second = store.acquire("bg/room", loads("pixels"));

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    CHECK(*first == *second);
    CHECK(loads.count == 1);
    CHECK(store.loaded_count() == 1);

    // Two requests, two references: the second caller owns one as much as the
    // first does, and neither can be the one that decides to unload.
    CHECK(store.reference_count(*first) == 2);
}

TEST_CASE("a handle names its asset, and a null handle names nothing", "[assets][store]")
{
    AssetStore<Counted> store;
    Loads loads;

    const auto handle = store.acquire("bg/room", loads("pixels"));
    REQUIRE(handle.has_value());

    REQUIRE(store.get(*handle) != nullptr);
    CHECK(store.get(*handle)->content == "pixels");

    trace_step("a default-constructed handle is the null one");
    CHECK(store.get(AssetHandle<Counted>{}) == nullptr);

    trace_step("and so is one from a slot that never existed");
    CHECK(store.get(AssetHandle<Counted>{.index = 99, .generation = 1}) == nullptr);
}

TEST_CASE("a failed load leaves nothing behind", "[assets][store]")
{
    AssetStore<Counted> store;
    int attempts = 0;

    const auto failing = [&attempts]() -> std::expected<Counted, Error>
    {
        ++attempts;
        return std::unexpected(make_error(ErrorCode::FILE_NOT_FOUND, "no such file"));
    };

    const auto first = store.acquire("bg/missing", failing);

    REQUIRE_FALSE(first.has_value());
    CHECK(first.error().code == ErrorCode::FILE_NOT_FOUND);
    CHECK(store.loaded_count() == 0);
    CHECK(store.slot_count() == 0);

    // A failure is not cached: whether it is worth remembering depends on what
    // the asset was for, which this table has no way of knowing.
    trace_step("so the next attempt is a real one, not a remembered failure");
    static_cast<void>(store.acquire("bg/missing", failing));
    CHECK(attempts == 2);
}

TEST_CASE("collection keeps what is held and frees what is not", "[assets][store]")
{
    const int alive_before = Counted::alive;

    AssetStore<Counted> store;
    Loads loads;

    const auto held = store.acquire("bg/room", loads("held"));
    const auto dropped = store.acquire("bg/hall", loads("dropped"));

    REQUIRE(held.has_value());
    REQUIRE(dropped.has_value());
    CHECK(Counted::alive == alive_before + 2);

    store.release(*dropped);

    trace_step("released is not unloaded: showing a sprite again must stay free");
    CHECK(store.get(*dropped) != nullptr);
    CHECK(Counted::alive == alive_before + 2);
    CHECK(store.loaded_count() == 2);

    trace_step("the game decides when memory goes back, and then it really does");
    CHECK(store.collect_unused() == 1);
    CHECK(Counted::alive == alive_before + 1);
    CHECK(store.loaded_count() == 1);
    CHECK(store.get(*held) != nullptr);
}

TEST_CASE("a handle to a collected asset is stale, never someone else's asset",
          "[assets][store]")
{
    AssetStore<Counted> store;
    Loads loads;

    const auto first = store.acquire("bg/room", loads("the first"));
    REQUIRE(first.has_value());

    store.release(*first);
    REQUIRE(store.collect_unused() == 1);

    CHECK(store.get(*first) == nullptr);

    const auto second = store.acquire("bg/hall", loads("the second"));
    REQUIRE(second.has_value());

    trace("slot {} reused at generation {}", second->index, second->generation);

    trace_step("the slot is reused, which is exactly why the generation exists");
    CHECK(second->index == first->index);
    CHECK(store.slot_count() == 1);

    // The failure this prevents is the one that looks like it works: without the
    // generation, the stale handle would now quietly name a different picture.
    CHECK(store.get(*first) == nullptr);
    REQUIRE(store.get(*second) != nullptr);
    CHECK(store.get(*second)->content == "the second");
}

TEST_CASE("a pointer stays valid while other assets load", "[assets][store]")
{
    AssetStore<Counted> store;
    Loads loads;

    const auto first = store.acquire("bg/room", loads("the first"));
    REQUIRE(first.has_value());

    const Counted* held = store.get(*first);
    REQUIRE(held != nullptr);

    // The hazard this rules out: a slot table in a vector reallocates, and every
    // pointer handed out before the load is dangling afterwards.
    for (int index = 0; index < 64; ++index)
    {
        static_cast<void>(store.acquire(std::format("bg/{}", index), loads("filler")));
    }

    CHECK(held == store.get(*first));
    CHECK(held->content == "the first");
}

TEST_CASE("releasing more often than acquiring is reported", "[assets][store]")
{
    AssetStore<Counted> store;
    Loads loads;

    const auto handle = store.acquire("bg/room", loads("pixels"));
    REQUIRE(handle.has_value());

    const LogCaptureGuard log_capture;

    CHECK(store.release(*handle));
    CHECK_FALSE(store.release(*handle));

    CHECK(log_capture.count(cpen::log::Level::ERROR) == 1);
    CHECK(store.reference_count(*handle) == 0);
}

TEST_CASE("a reference gives itself back", "[assets][store]")
{
    AssetStore<Counted> store;
    Loads loads;

    const auto handle = store.acquire("bg/room", loads("pixels"));
    REQUIRE(handle.has_value());

    {
        const AssetReference<Counted> reference(store, *handle);

        CHECK(static_cast<bool>(reference));
        CHECK(reference->content == "pixels");
        CHECK(store.reference_count(*handle) == 1);

        trace_step("a copy is another reference, as a shared_ptr would be");
        {
            const AssetReference<Counted> copy = reference;
            CHECK(store.reference_count(*handle) == 2);
            CHECK(copy.handle() == *handle);
        }

        CHECK(store.reference_count(*handle) == 1);

        trace_step("a move is the same reference in a different place");
        AssetReference<Counted> moved = AssetReference<Counted>(reference);
        CHECK(store.reference_count(*handle) == 2);

        const AssetReference<Counted> adopted = std::move(moved);
        CHECK(store.reference_count(*handle) == 2);
        CHECK_FALSE(static_cast<bool>(moved));
    }

    trace_step("and the scope ending is the whole point");
    CHECK(store.reference_count(*handle) == 0);
    CHECK(store.get(*handle) != nullptr);
}

TEST_CASE("an empty reference is safe to use", "[assets][store]")
{
    const AssetReference<Counted> reference;

    CHECK_FALSE(static_cast<bool>(reference));
    CHECK(reference.get() == nullptr);
    CHECK_FALSE(reference.handle().valid());
}
