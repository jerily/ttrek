#ifndef ARENA_H
#define ARENA_H

#include <vector>
#include <cstddef>
#include <cassert>

const std::size_t CHUNK_SIZE = 128;

template<typename TId, typename TValue>
class Arena {
private:
    std::vector<std::vector<TValue>> chunks;
    std::size_t len;

public:
// Constructs a new arena
    Arena() : len(0) {
        chunks.emplace_back();
        chunks.back().reserve(CHUNK_SIZE);
    }

// Clears all entries from the arena
    void clear() {
        len = 0;
        for (auto &chunk: chunks) {
            chunk.clear();
        }
    }

// Constructs a new arena with a capacity for `n` values pre-allocated
    static Arena with_capacity(std::size_t n) {
        Arena arena;
        n = std::max<std::size_t>(1, n);
        std::size_t n_chunks = (n + CHUNK_SIZE - 1) / CHUNK_SIZE;
        arena.chunks.reserve(n_chunks);
        for (std::size_t i = 0; i < n_chunks; ++i) {
            arena.chunks.emplace_back();
            arena.chunks.back().reserve(CHUNK_SIZE);
        }
        return arena;
    }

// Returns the size of the arena
    std::size_t size() const {
        return len;
    }

// Allocates a new instance of TValue and returns an Id that can be used to reference it
    TId alloc(const TValue &value) {
        std::size_t id = len;
        auto [chunk_idx, offset] = chunk_and_offset(id);
        if (chunk_idx >= chunks.size()) {
            chunks.emplace_back();
            chunks.back().reserve(CHUNK_SIZE);
        }
        chunks[chunk_idx].push_back(value);
        len++;
        return TId::from_usize(id);
    }

// Indexing into the arena
    const TValue &operator[](TId id) const {
        auto [chunk, offset] = chunk_and_offset(id.to_usize());
        assert(chunk < chunks.size() && offset < chunks[chunk].size());
        return chunks[chunk][offset];
    }

    TValue &operator[](TId id) {
        auto [chunk, offset] = chunk_and_offset(id.to_usize());
        assert(chunk < chunks.size() && offset < chunks[chunk].size());
        return chunks[chunk][offset];
    }

private:
// Helper function to calculate chunk and offset
    static std::pair<std::size_t, std::size_t> chunk_and_offset(std::size_t index) {
        std::size_t offset = index % CHUNK_SIZE;
        std::size_t chunk = index / CHUNK_SIZE;
        return {chunk, offset};
    }
};

#endif // ARENA_H
