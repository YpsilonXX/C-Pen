#ifndef CPEN_ASSETS_ASSET_STORE_HH
#define CPEN_ASSETS_ASSET_STORE_HH

#include "cpen/core/error.hh"
#include "cpen/core/log.hh"

#include <cstdint>
#include <deque>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cpen::assets
{
    /// A reference to a loaded asset: an index into a slot table and the
    /// generation that slot had when the handle was made.
    ///
    /// An integer rather than a pointer or a shared_ptr, because a handle is
    /// something a save file, a script value and a scene description all have to
    /// be able to hold, and because the thing it refers to must be able to move
    /// in memory without every holder learning about it.
    ///
    /// The generation is what makes a stale handle detectable instead of
    /// dangerous. Slots are reused; without it, a handle to an asset that was
    /// collected would quietly start naming whatever took its place — the worst
    /// possible failure, because it looks like it works. Generation 0 never
    /// occurs in a live slot, so a default-constructed handle is the null one.
    template <typename Asset>
    struct AssetHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        bool valid() const noexcept { return this->generation != 0; }

        friend bool operator==(const AssetHandle&, const AssetHandle&) noexcept = default;
    };

    /// A table of loaded assets of one type, keyed by a string and referred to by
    /// handle.
    ///
    /// A template rather than one type-erased container, which is what keeps the
    /// whole mechanism — slots, generations, reference counts, collection —
    /// testable with no graphics driver anywhere near it: the tests instantiate it
    /// over an ordinary type and exercise every path, while the engine
    /// instantiates it over Texture and Font. Type erasure would have bought
    /// nothing here except casts.
    ///
    /// Nothing is unloaded when its last reference goes. An asset with no
    /// references is a cached asset, and a game that shows a sprite, hides it and
    /// shows it again is the normal case, not the exception. Memory is reclaimed
    /// when the game says so, by calling collect_unused — typically between
    /// chapters, where a pause is expected and nothing is on screen to stutter.
    template <typename Asset>
    class AssetStore
    {
    public:
        using Handle = AssetHandle<Asset>;

        /// Returns a handle to the asset under `key`, loading it if this is the
        /// first request.
        ///
        /// `load` is called only on a miss, and only its success reaches the
        /// table: a failed load leaves no slot, no key and no handle behind, so
        /// the next attempt is a fresh one rather than a cached failure. Whether
        /// a failure is worth remembering is a policy decision, and it belongs
        /// with the layer that knows what the asset was for.
        ///
        /// The returned handle carries one reference, which the caller owns and
        /// must release — or, better, hand to an AssetReference and forget about.
        template <typename Loader>
        std::expected<Handle, core::Error> acquire(const std::string_view key, Loader&& load)
        {
            if (const auto found = this->keys.find(std::string{key}); found != this->keys.end())
            {
                Slot& slot = this->slots[found->second];
                ++slot.references;

                return Handle{.index = found->second, .generation = slot.generation};
            }

            std::expected<Asset, core::Error> loaded = std::forward<Loader>(load)();

            if (!loaded.has_value())
            {
                return std::unexpected(std::move(loaded.error()));
            }

            const std::uint32_t index = this->claim_slot();
            Slot& slot = this->slots[index];

            slot.asset.emplace(std::move(*loaded));
            slot.key = std::string{key};
            slot.references = 1;

            this->keys.emplace(slot.key, index);

            return Handle{.index = index, .generation = slot.generation};
        }

        /// The asset a handle names, or nullptr if the handle is null, stale, or
        /// from a different store.
        ///
        /// A pointer rather than a reference because "the asset is gone" has to be
        /// expressible: a handle outliving what it named is a bug, and one that
        /// must be visible at the point it is used rather than three frames later.
        Asset* get(const Handle handle) noexcept
        {
            Slot* slot = this->slot_for(handle);
            return slot == nullptr ? nullptr : &*slot->asset;
        }

        const Asset* get(const Handle handle) const noexcept
        {
            const Slot* slot = this->slot_for(handle);
            return slot == nullptr ? nullptr : &*slot->asset;
        }

        bool contains(const std::string_view key) const
        {
            return this->keys.contains(std::string{key});
        }

        /// Adds a reference. Answers false for a handle that names nothing.
        bool add_reference(const Handle handle) noexcept
        {
            Slot* slot = this->slot_for(handle);

            if (slot == nullptr)
            {
                return false;
            }

            ++slot->references;
            return true;
        }

        /// Drops a reference. The asset stays loaded either way; see the class
        /// comment.
        bool release(const Handle handle) noexcept
        {
            Slot* slot = this->slot_for(handle);

            if (slot == nullptr)
            {
                return false;
            }

            if (slot->references == 0)
            {
                // Released more often than acquired. Nothing is corrupted — the
                // count is already at rest — but somebody's bookkeeping is wrong
                // and will strand or double-free something else later.
                log::error(log::Category::ASSETS,
                           "asset '{}' released more times than it was acquired",
                           slot->key);
                return false;
            }

            --slot->references;
            return true;
        }

        std::uint32_t reference_count(const Handle handle) const noexcept
        {
            const Slot* slot = this->slot_for(handle);
            return slot == nullptr ? 0 : slot->references;
        }

        /// Unloads every asset nobody holds a reference to, and answers how many.
        ///
        /// Every handle to a collected asset becomes stale at once, which is the
        /// point of the generation counter: the mistake of keeping one is caught
        /// rather than rewarded with a different asset.
        std::size_t collect_unused()
        {
            std::size_t collected = 0;

            for (std::uint32_t index = 0; index < this->slots.size(); ++index)
            {
                Slot& slot = this->slots[index];

                if (!slot.asset.has_value() || slot.references > 0)
                {
                    continue;
                }

                this->keys.erase(slot.key);

                slot.asset.reset();
                slot.key.clear();
                ++slot.generation;

                this->free_slots.push_back(index);
                ++collected;
            }

            return collected;
        }

        /// How many assets are loaded, held or merely cached.
        std::size_t loaded_count() const noexcept
        {
            return this->keys.size();
        }

        /// How many slots exist, including free ones. Of interest to a test
        /// asserting that slots are reused rather than to a game.
        std::size_t slot_count() const noexcept
        {
            return this->slots.size();
        }

    private:
        struct Slot
        {
            std::optional<Asset> asset;
            std::string key;

            /// Starts at 1 so that a default-constructed handle, whose generation
            /// is 0, never matches a live slot.
            std::uint32_t generation = 1;

            std::uint32_t references = 0;
        };

        std::uint32_t claim_slot()
        {
            if (!this->free_slots.empty())
            {
                const std::uint32_t index = this->free_slots.back();
                this->free_slots.pop_back();
                return index;
            }

            this->slots.emplace_back();
            return static_cast<std::uint32_t>(this->slots.size() - 1);
        }

        Slot* slot_for(const Handle handle) noexcept
        {
            return const_cast<Slot*>(std::as_const(*this).slot_for(handle));
        }

        const Slot* slot_for(const Handle handle) const noexcept
        {
            if (!handle.valid() || handle.index >= this->slots.size())
            {
                return nullptr;
            }

            const Slot& slot = this->slots[handle.index];

            if (slot.generation != handle.generation || !slot.asset.has_value())
            {
                return nullptr;
            }

            return &slot;
        }

        /// A deque, not a vector: get() hands out a pointer into this container,
        /// and loading a second asset must not invalidate the first one's. The
        /// index-based handle needs random access, which rules out a list, and
        /// this is the container that gives both.
        std::deque<Slot> slots;

        std::vector<std::uint32_t> free_slots;

        std::unordered_map<std::string, std::uint32_t> keys;
    };

    /// Owns one reference to an asset and gives it back on destruction.
    ///
    /// The handle is what gets stored and serialised; this is what ordinary code
    /// holds, so that the release cannot be forgotten on an early return or in
    /// the middle of an error path. Copying takes another reference, as a
    /// shared_ptr would — a scene handing an asset to the thing that draws it is
    /// the common case, and making that a move would mean the caller has to think
    /// about which copy is the real one.
    template <typename Asset>
    class AssetReference
    {
    public:
        using Handle = AssetHandle<Asset>;

        AssetReference() noexcept = default;

        /// Adopts an existing reference — the one acquire() returned. Does not
        /// take a second one, so the usual flow costs exactly one.
        AssetReference(AssetStore<Asset>& asset_store, const Handle handle) noexcept
            : store(&asset_store), asset_handle(handle) {}

        ~AssetReference() { this->reset(); }

        AssetReference(const AssetReference& other) noexcept
            : store(other.store), asset_handle(other.asset_handle)
        {
            if (this->store != nullptr)
            {
                this->store->add_reference(this->asset_handle);
            }
        }

        AssetReference(AssetReference&& other) noexcept
            : store(other.store), asset_handle(other.asset_handle)
        {
            other.store = nullptr;
            other.asset_handle = Handle{};
        }

        AssetReference& operator=(const AssetReference& other) noexcept
        {
            if (this != &other)
            {
                AssetReference copy(other);
                this->swap(copy);
            }

            return *this;
        }

        AssetReference& operator=(AssetReference&& other) noexcept
        {
            if (this != &other)
            {
                this->reset();
                this->store = other.store;
                this->asset_handle = other.asset_handle;
                other.store = nullptr;
                other.asset_handle = Handle{};
            }

            return *this;
        }

        void swap(AssetReference& other) noexcept
        {
            std::swap(this->store, other.store);
            std::swap(this->asset_handle, other.asset_handle);
        }

        /// Gives up the reference and becomes empty.
        void reset() noexcept
        {
            if (this->store != nullptr)
            {
                this->store->release(this->asset_handle);
                this->store = nullptr;
                this->asset_handle = Handle{};
            }
        }

        Handle handle() const noexcept { return this->asset_handle; }

        Asset* get() const noexcept
        {
            return this->store == nullptr ? nullptr : this->store->get(this->asset_handle);
        }

        Asset* operator->() const noexcept { return this->get(); }
        Asset& operator*() const noexcept { return *this->get(); }

        explicit operator bool() const noexcept { return this->get() != nullptr; }

    private:
        AssetStore<Asset>* store = nullptr;
        Handle asset_handle;
    };
}

#endif //CPEN_ASSETS_ASSET_STORE_HH
