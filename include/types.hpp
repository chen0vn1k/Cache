#pragma once

//#include <cstdint>
#include <cstddef>
//#include <array>
//#include <algorithm>
//#include <optional>

namespace cache {

struct CacheConfig {
  size_t cache_size = 65536; // 64 KB
  size_t block_size = 64;
  size_t associativity = 1;
};

} // namespace cache


