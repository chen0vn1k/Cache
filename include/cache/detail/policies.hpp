#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <string>
#include <map>

namespace cache {

// --- ПОЛИТИКИ ЗАПИСИ ---

//Write-Back
struct WriteBackPolicy
{
  enum class State { INVALID, CLEAN, DIRTY };

  struct BlockMeta
  {
    State state = State::INVALID;
  };

  // Функции
  
  // Проверка существования данных в блоке
  static bool is_valid(const BlockMeta& meta) noexcept 
  {
    return meta.state != State::INVALID;
  }

  // Проверка измененности данных в блоке
  static bool is_dirty(const BlockMeta& meta) noexcept
  {
    return meta.state == State::DIRTY;
  }

  // Проверка необходимости передачи записи ниже по иерархии
  static constexpr bool requires_write_through() noexcept
  {
    return false;
  }

  // Обработка метаданных при нахождении блока
  static void on_hit(BlockMeta& meta, bool is_write) noexcept
  { 
    if (is_write)
    {
      meta.state = State::DIRTY;
    }
  }

  // Обработка метаданных при отсуствии блока
  static void on_miss(BlockMeta& meta, bool is_write) noexcept
  {
    meta.state = is_write ? State::DIRTY : State::CLEAN;
  }

  // Имя состояния (для трассировки)
  static const char* state_name(State s) noexcept
  {
    switch (s)
    {
      case State::INVALID: return "INVALID";
      case State::CLEAN:   return "CLEAN";
      case State::DIRTY:   return "DIRTY";
    }
    return "?";
  }

  static const char* state_name(const BlockMeta& meta) noexcept
  {
    return state_name(meta.state);
  }

  // Метаданные для трассировки (имя → значение)
  static std::map<std::string, std::string> describe(const BlockMeta& meta)
  {
    return {{"state", state_name(meta)}};
  }

};

// Write-Through
struct WriteThroughPolicy
{
  enum class State { INVALID, VALID };

  struct BlockMeta
  { 
    State state = State::INVALID; 
  };

  // Функции
  
  // Проверка существования данных в блоке
  static bool is_valid(const BlockMeta& meta) noexcept
  {
    return meta.state == State::VALID;
  }

  // Проверка измененности данных в блоке
  static bool is_dirty(const BlockMeta& /*meta*/) noexcept
  {
    return false;
  }

  // Проверка необходимости передачи записи ниже по иерархии
  static constexpr bool requires_write_through() noexcept
  {
    return true;
  }

  // Обработка метаданных при нахождении блока
  static void on_hit(BlockMeta& meta, bool /*is_write*/) noexcept
  {}

  // Обработка метаданных при отсуствии блока
  static void on_miss(BlockMeta& meta, bool /*is_write*/) noexcept
  {
    meta.state = State::VALID;
  }

  static const char* state_name(State s) noexcept
  {
    switch (s)
    {
      case State::INVALID: return "INVALID";
      case State::VALID:   return "VALID";
    }
    return "?";
  }

  static const char* state_name(const BlockMeta& meta) noexcept
  {
    return state_name(meta.state);
  }

  static std::map<std::string, std::string> describe(const BlockMeta& meta)
  {
    return {
      {"state", state_name(meta)}
    };
  }
};


// --- ПОЛИТИКИ ЗАВЕДЕНИЯ ---

// Промах по чтению
struct NoWriteAllocatePolicy
{
  struct AllocateMeta
  {};

  // Заведение по записи
  static bool need_write_allocate(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept
  {
    return false;
  }

  // Заведение по чтению
  static bool need_read_allocate(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept
  {
    return  true;
  }

  // Обработка попадания
  static void hit_handle(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept
  {}

  static std::map<std::string, std::string> describe(const AllocateMeta& /*meta*/)
  {
    return {};
  }

};

// Промах по записи
struct NoReadAllocatePolicy
{
  struct AllocateMeta
  {};

  // Заведение по записи
  static bool need_write_allocate(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept
  {
    return true;
  }

  // Заведение по чтению
  static bool need_read_allocate(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept
  {
    return  false;
  }

  // Обработка попадания
  static void hit_handle(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept
  {}

  static std::map<std::string, std::string> describe(const AllocateMeta& /*meta*/)
  {
    return {};
  }
};

// Промах по записи и по чтению
struct AllAllocatePolicy
{
  struct AllocateMeta
  {};

  // Заведение по записи
  static bool need_write_allocate(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept
  {
    return true;
  }

  // Заведение по чтению
  static bool need_read_allocate(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept
  {
    return  true;
  }

  // Обработка попадания
  static void hit_handle(AllocateMeta& /*meta*/, uint64_t /*block_address*/) noexcept
  {}

  static std::map<std::string, std::string> describe(const AllocateMeta& /*meta*/)
  {
    return {};
  }
};


// --- ПОЛИТИКИ ЗАМЕЩЕНИЯ ---

// Least Recently Used
struct LRUPolicy
{
  struct BlockMeta
  {
    uint32_t age = 0;
  };

  // Обновление метаданных политики в наборе
  template <typename BlockType>
  static void touch(BlockType& target, std::vector<BlockType>& set) noexcept
  {
    // увеличиваем возраст всех кроме выбранного
    for (auto& line : set)
    {
      if (&line != &target)
      {
        static_cast<BlockMeta&>(line).age++;
      }
    }
    static_cast<BlockMeta&>(target).age = 0;
  }

  // Выбор удаляемого блока
  template <typename BlockType>
  static BlockType& select_victim(std::vector<BlockType>& set) noexcept
  {
    return *std::max_element(set.begin(), set.end(),
      [](const BlockType& a, const BlockType& b)
      {
        return static_cast<const BlockMeta&>(a).age < static_cast<const BlockMeta&>(b).age;
      });
  }

  // Метаданные для трассировки
  static std::map<std::string, std::string> describe(const BlockMeta& meta)
  {
    return {{"age", std::to_string(meta.age)}};
  }
};

// Most Recently Used
struct MRUPolicy
{
  struct BlockMeta
  {
    uint32_t age = 0;
  };

  // Обновление метаданных политики в наборе
  template <typename BlockType>
  static void touch(BlockType& target, std::vector<BlockType>& set) noexcept
  {
    // увеличиваем возраст всех кроме выбранного
    for (auto& line : set)
    {
      if (&line != &target)
      {
        static_cast<BlockMeta&>(line).age++;
      }
    }
    static_cast<BlockMeta&>(target).age = 0;
  }

  // Выбор удаляемого блока
  template <typename BlockType>
  static BlockType& select_victim(std::vector<BlockType>& set) noexcept
  {
    return *std::min_element(set.begin(), set.end(),
      [](const BlockType& a, const BlockType& b)
      {
        return static_cast<const BlockMeta&>(a).age < static_cast<const BlockMeta&>(b).age;
      });
  }

  static std::map<std::string, std::string> describe(const BlockMeta& meta)
  {
    return {{"age", std::to_string(meta.age)}};
  }
};

// Least Frequently Used
struct LFUPolicy
{
  struct BlockMeta
  {
    uint32_t frequency = 0;
  };

  // Обновление метаданных политики в наборе
  template <typename BlockType>
  static void touch(BlockType& target, std::vector<BlockType>& /*set*/) noexcept
  {
    static_cast<BlockMeta&>(target).frequency++;
  }

  // Выбор удаляемого блока
  template <typename BlockType>
  static BlockType& select_victim(std::vector<BlockType>& set) noexcept
  {
    return *std::min_element(set.begin(), set.end(),
      [](const BlockType& a, const BlockType& b)
      {
        return static_cast<const BlockMeta&>(a).frequency < static_cast<const BlockMeta&>(b).frequency;
      });
  }

  static std::map<std::string, std::string> describe(const BlockMeta& meta)
  {
    return {{"frequency", std::to_string(meta.frequency)}};
  }
};

// Most Frequently Used
struct MFUPolicy
{
  struct BlockMeta
  {
    uint32_t frequency = 0;
  };

  // Обновление метаданных политики в наборе
  template <typename BlockType>
  static void touch(BlockType& target, std::vector<BlockType>& /*set*/) noexcept
  {
    static_cast<BlockMeta&>(target).frequency++;
  }

  // Выбор удаляемого блока
  template <typename BlockType>
  static BlockType& select_victim(std::vector<BlockType>& set) noexcept
  {
    return *std::max_element(set.begin(), set.end(),
      [](const BlockType& a, const BlockType& b)
      {
        return static_cast<const BlockMeta&>(a).frequency < static_cast<const BlockMeta&>(b).frequency;
      });
  }

  static std::map<std::string, std::string> describe(const BlockMeta& meta)
  {
    return {{"frequency", std::to_string(meta.frequency)}};
  }
};

// First-In First-Out
struct FIFOPolicy
{
  struct BlockMeta
  { 
    uint64_t insertion_tick = 0; 
    bool is_initialized = false;
  };

  // Обновление метаданных политики в наборе
  template <typename BlockType>
  static void touch(BlockType& target, std::vector<BlockType>& set) noexcept
  {
    auto& meta = static_cast<BlockMeta&>(target);
    
    // Ставим тик только один раз при записи в блок
    if (!meta.is_initialized)
    {
      uint64_t max_tick = 0;
      for (const auto& line : set)
      {
        const auto& m = static_cast<const BlockMeta&>(line);
        // Находим самый большой тик 
        if (m.is_initialized && m.insertion_tick > max_tick)
        {
          max_tick = m.insertion_tick;
        }
      }
      // Определяем тик у нового блока
      meta.insertion_tick = max_tick + 1;
      meta.is_initialized = true;
    }
  }

  // Выбор удаляемого блока
  template <typename BlockType>
  static BlockType& select_victim(std::vector<BlockType>& set) noexcept
  {
    // Жертва - строка с минимальным тиком вставки (добавлена раньше всех)
    return *std::min_element(set.begin(), set.end(),
      [](const BlockType& a, const BlockType& b)
      {
        return static_cast<const BlockMeta&>(a).insertion_tick < static_cast<const BlockMeta&>(b).insertion_tick;
      });
  }

  static std::map<std::string, std::string> describe(const BlockMeta& meta)
  {
    std::map<std::string, std::string> m;
    m["ins_tick"] = std::to_string(meta.insertion_tick);
    m["init"]    = meta.is_initialized ? "1" : "0";
    return m;
  }
};

// Вспомогательная функция: собрать метаданные со всех политик блока
template <typename ReplacementPolicy, typename WritePolicy, typename Block>
std::map<std::string, std::string> collect_metadata(const Block& block)
{
  std::map<std::string, std::string> result;

  // Метаданные политики замещения
  auto repl = ReplacementPolicy::describe(
      static_cast<const typename ReplacementPolicy::BlockMeta&>(block));
  result.merge(std::move(repl));

  // Метаданные политики записи 
  auto wr = WritePolicy::describe(
      static_cast<const typename WritePolicy::BlockMeta&>(block));
  // state обычно печатаем отдельно через state=, поэтому убираем дубликат
  result.merge(std::move(wr));

  return result;
}

} // namespace cache
