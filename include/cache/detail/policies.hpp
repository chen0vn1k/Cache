#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>

namespace cache {

// --- ПОЛИТИКИ ЗАПИСИ ---

//Write-Back
struct WriteBackPolicy {
  enum class State { INVALID, CLEAN, DIRTY };
  struct BlockMeta { State state = State::INVALID; };

  // Функции
  static bool is_valid(const BlockMeta& meta) noexcept { return meta.state != State::INVALID; }

  static bool is_dirty(const BlockMeta& meta) noexcept { return meta.state == State::DIRTY; }

  static constexpr bool requires_write_through() noexcept { return false; }


  static void on_hit(BlockMeta& meta, bool is_write) noexcept { 
    if (is_write) { meta.state = State::DIRTY; }
  }

  static void on_miss(BlockMeta& meta, bool is_write) noexcept {
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
  static void on_hit(BlockMeta& meta, bool /*is_write*/) noexcept {}
  // При чтении меняет на VALID
  static void on_miss(BlockMeta& meta, bool /*is_write*/) noexcept { meta.state = State::VALID; }
};


// --- ПОЛИТИКИ ЗАВЕДЕНИЯ ---

// Промах по чтению
struct NoWriteAllocatePolicy {
  struct AllocateMeta {};

  // Заведение по записи
  static bool need_write_allocate(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept {
    return false;
  }
  // Заведение по чтению
  static bool need_read_allocate(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept {
    return  true;
  }
  // Обработка попадания
  static void hit_handle(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept {}
};

// Промах по записи
struct NoReadAllocatePolicy {
  struct AllocateMeta {};

  // Заведение по записи
  static bool need_write_allocate(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept {
    return true;
  }
  // Заведение по чтению
  static bool need_read_allocate(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept {
    return  false;
  }
  // Обработка попадания
  static void hit_handle(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept {}
};

// Промах по записи и по чтению
struct AllAllocatePolicy {
  struct AllocateMeta {};

  // Заведение по записи
  static bool need_write_allocate(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept {
    return true;
  }
  // Заведение по чтению
  static bool need_read_allocate(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept {
    return  true;
  }
  // Обработка попадания
  static void hit_handle(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept {}
};


// --- ПОЛИТИКИ ЗАМЕЩЕНИЯ ---

// Least Recently Used
struct LRUPolicy {
  struct BlockMeta { uint32_t age = 0; };

  // Обновление метаданных политики в наборе
  template <typename BlockType>
  static void touch(BlockType& target, std::vector<BlockType>& set) noexcept {
    // увеличиваем возраст всех кроме выбранного
    for (auto& line : set) {
      if (&line != &target) { static_cast<BlockMeta&>(line).age++; }
    }
    static_cast<BlockMeta&>(target).age = 0;
  }

  // Выбор удаляемого блока
  template <typename BlockType>
  static BlockType& select_victim(std::vector<BlockType>& set) noexcept {
    return *std::max_element(set.begin(), set.end(),
      [](const BlockType& a, const BlockType& b) {
        return static_cast<const BlockMeta&>(a).age < static_cast<const BlockMeta&>(b).age;
      });
  }
};

// Most Recently Used
struct MRUPolicy {
  struct BlockMeta { uint32_t age = 0; };

  // Обновление метаданных политики в наборе
  template <typename BlockType>
  static void touch(BlockType& target, std::vector<BlockType>& set) noexcept {
    // увеличиваем возраст всех кроме выбранного
    for (auto& line : set) {
      if (&line != &target) { static_cast<BlockMeta&>(line).age++; }
    }
    static_cast<BlockMeta&>(target).age = 0;
  }

  // Выбор удаляемого блока
  template <typename BlockType>
  static BlockType& select_victim(std::vector<BlockType>& set) noexcept {
    return *std::min_element(set.begin(), set.end(),
      [](const BlockType& a, const BlockType& b) {
        return static_cast<const BlockMeta&>(a).age < static_cast<const BlockMeta&>(b).age;
      });
  }
};

// Least Frequently Used
struct LFUPolicy {
  struct BlockMeta { uint32_t frequency = 0; };

  // Обновление метаданных политики в наборе
  template <typename BlockType>
  static void touch(BlockType& target, std::vector<BlockType>& /*set*/) noexcept {
    static_cast<BlockMeta&>(target).frequency++;
  }

  // Выбор удаляемого блока
  template <typename BlockType>
  static BlockType& select_victim(std::vector<BlockType>& set) noexcept {
    return *std::min_element(set.begin(), set.end(),
      [](const BlockType& a, const BlockType& b) {
        return static_cast<const BlockMeta&>(a).frequency < static_cast<const BlockMeta&>(b).frequency;
      });
  }
};

// Most Frequently Used
struct MFUPolicy {
  struct BlockMeta { uint32_t frequency = 0; };

  // Обновление метаданных политики в наборе
  template <typename BlockType>
  static void touch(BlockType& target, std::vector<BlockType>& /*set*/) noexcept {
    static_cast<BlockMeta&>(target).frequency++;
  }

  // Выбор удаляемого блока
  template <typename BlockType>
  static BlockType& select_victim(std::vector<BlockType>& set) noexcept {
    return *std::max_element(set.begin(), set.end(),
      [](const BlockType& a, const BlockType& b) {
        return static_cast<const BlockMeta&>(a).frequency < static_cast<const BlockMeta&>(b).frequency;
      });
  }
};

// First-In First-Out
struct FIFOPolicy {
  struct BlockMeta { 
    uint64_t insertion_tick = 0; 
    bool is_initialized = false;
  };

  // Обновление метаданных политики в наборе
  template <typename BlockType>
  static void touch(BlockType& target, std::vector<BlockType>& set) noexcept {
    auto& meta = static_cast<BlockMeta&>(target);
    
    // Ставим тик только один раз при записи в блок
    if (!meta.is_initialized) {
      uint64_t max_tick = 0;
      for (const auto& line : set) {
        const auto& m = static_cast<const BlockMeta&>(line);
        // Находим самый большой тик 
        if (m.is_initialized && m.insertion_tick > max_tick)
          max_tick = m.insertion_tick;
      }
      // Определяем тик у нового блока
      meta.insertion_tick = max_tick + 1;
      meta.is_initialized = true;
    }
  }

  // Выбор удаляемого блока
  template <typename BlockType>
  static BlockType& select_victim(std::vector<BlockType>& set) noexcept {
    // Жертва - строка с минимальным тиком вставки (добавлена раньше всех)
    return *std::min_element(set.begin(), set.end(), [](const BlockType& a, const BlockType& b) {
      return static_cast<const BlockMeta&>(a).insertion_tick < static_cast<const BlockMeta&>(b).insertion_tick;
    });
  }
};

} // namespace cache
