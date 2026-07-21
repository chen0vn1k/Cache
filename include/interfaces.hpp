#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include "types.hpp"

namespace cache {

// Шина CPU -> Cache (Запрос от процессора)
struct CpuRequest {
  uint64_t address;
  OperationType op_type;
  std::vector<std::byte> data;
};

// Шина Cache -> CPU (Ответ процессору)
struct CpuResponse {
  bool hit;
  std::vector<std::byte> data;
};

// Шина Cache -> Memory (Запрос в  память на чтение/запись строки)
struct MemRequest {
  uint64_t address;
  OperationType op_type; 
  std::vector<std::byte> data; 
};

// Шина Memory -> Cache (Ответ от памяти)
struct MemResponse {
  bool ready;
  std::vector<std::byte> data;
};

} // namespace cache
