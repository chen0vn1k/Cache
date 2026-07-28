#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>

namespace cache {

//Write-Back
struct WriteBackPolicy {
  enum class State { INVALID, CLEAN, DIRTY };
  struct BlockMeta { State state = State::INVALID; };

  static bool is_valid(const BlockMeta& meta) noexcept { return meta.state != State::INVALID; }
  static bool is_dirty(const BlockMeta& meta) noexcept { return meta.state == State::DIRTY; }
  static constexpr bool requires_write_through() noexcept { return false; }

  static void on_read_hit(BlockMeta& /*meta*/) noexcept {}
  static void on_write_hit(BlockMeta& meta) noexcept { meta.state = State::DIRTY; }
  static void on_write_miss(BlockMeta& meta, bool is_write) noexcept {
    meta.state = is_write ? State::DIRTY : State::CLEAN;
  }
};

// Write-Through
struct WriteThroughPolicy {
  enum class State { INVALID, VALID };

  struct BlockMeta { 
    State state = State::INVALID; 
  };

  // Флаги для контроллера
  static bool is_valid(const BlockMeta& meta) noexcept { return meta.state == State::VALID; }
  static bool is_dirty(const BlockMeta& /*meta*/) noexcept { return false; } // Никогда не нужна выгрузка при вытеснении
  static constexpr bool requires_write_through() noexcept { return true; } // Сигнал для контроллера

  // Переходы FSM
  static void on_read_hit(BlockMeta& /*meta*/) noexcept {}
  
  static void on_write_hit(BlockMeta& /*meta*/) noexcept {}
  
  // При чтении меняет на VALID
  static void on_write_miss(BlockMeta& meta, bool /*is_write*/) noexcept {
    meta.state = State::VALID;
  }
};

// --- ПОЛИТИКА АЛЛОКАЦИИ ---
struct WriteAllocatePolicy {
  struct BlockMeta {}; // 0 bytes overhead
  static constexpr bool write_allocate() noexcept { return true; }
};

// --- ПОЛИТИКА ЗАМЕЩЕНИЯ (LRU) ---
struct LRUPolicy {
  struct BlockMeta { uint32_t age = 0; };

  template <typename BlockType>
  static void touch(BlockType& target, std::vector<BlockType>& set) noexcept {
    // увеличиваем возраст всех кроме выбранного
    for (auto& line : set) {
      if (&line != &target) static_cast<BlockMeta&>(line).age++;
    }
    static_cast<BlockMeta&>(target).age = 0;
  }

  template <typename BlockType>
  static BlockType& select_victim(std::vector<BlockType>& set) noexcept {
    return *std::max_element(set.begin(), set.end(),
      [](const BlockType& a, const BlockType& b) {
        return static_cast<const BlockMeta&>(a).age < static_cast<const BlockMeta&>(b).age;
      });
  }
};

// FIFO
struct FIFOPolicy {
    struct BlockMeta { 
        uint64_t insertion_tick = 0; 
        bool is_initialized = false;
    };

    // Глобальный счетчик для отслеживания порядка вставки
    static inline uint64_t current_tick = 0;

    template <typename BlockType>
    static void touch(BlockType& target, std::vector<BlockType>& /*set*/) noexcept {
      current_tick++;
      auto& meta = static_cast<BlockMeta&>(target);
      
      // В отличие от LRU, при попадании (hit) время вставки не должно обновляться.
      // Метод CacheBlock::reset() автоматически сбрасывает метаданные политик (is_initialized = false).
      // Поэтому tick обновится ТОЛЬКО при первой загрузке блока в пустую/вытесненную линию.
      if (!meta.is_initialized) {
        meta.insertion_tick = current_tick;
        meta.is_initialized = true;
      }
    }

    template <typename BlockType>
    static BlockType& select_victim(std::vector<BlockType>& set) noexcept {
      // Жертва - строка с минимальным тиком вставки (добавлена раньше всех)
      return *std::min_element(set.begin(), set.end(),
        [](const BlockType& a, const BlockType& b) {
          return static_cast<const BlockMeta&>(a).insertion_tick < static_cast<const BlockMeta&>(b).insertion_tick;
        });
    }
};
// --- ПОЛИТИКА ТРАССИРОВКИ ---
struct NoTracer {
    NoTracer() = default;
    inline void trace_access(uint64_t, bool) {}
    inline void trace_hit(uint64_t) {}
    inline void trace_miss(uint64_t) {}
    inline void trace_evict(uint64_t) {}
};

} // namespace cache
