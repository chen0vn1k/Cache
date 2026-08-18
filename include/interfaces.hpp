#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <variant>

//#include "types.hpp"

namespace cache {

// Запросы
// =========================================================================
struct ReadRequest
{
  uint64_t address;
  size_t size; // Количество запрашиваемых байт
};

struct WriteRequest
{
  uint64_t address;
  std::vector<std::byte> data; // Записываемые данные
};


// Единый тип запроса
using Request = std::variant<ReadRequest, WriteRequest>;
// =========================================================================

// Oтветы
// =========================================================================
struct ReadResponse
{
  std::vector<std::byte> data; // Возвращаемые байты
  bool success = true;
};

struct WriteResponse
{
  bool success = true; // Сигнал подтверждения записи
};

// Единый тип ответа
using Response = std::variant<ReadResponse, WriteResponse>;
// =========================================================================

}// namespace cache
