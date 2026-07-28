#pragma once

//#include <cstdint>
#include <cstddef>
//#include <array>
//#include <algorithm>
//#include <optional>

namespace cache {

// FSM

// FSM для кэш-линии
enum class BlockState {
  // Для write-back
  INVALID,
  VALID,
  DIRTY
};

// Политика вытестения
enum class ReplacementPolicy {
  LRU,
  FIFO
};

// Политика записи (write allocate)
enum class WritePolicy {
  WRITE_BACK,
  WRITE_THROUGH
};

enum class OperationType {
  READ,
  WRITE,
  INVALIDATE
};

// параметры в шаблон кэша
struct CacheConfig {
  size_t cache_size = 65536; // 64 KB
  size_t block_size = 64;
  size_t associativity = 1;
};

} // namespace cache


