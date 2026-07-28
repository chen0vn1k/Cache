#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <algorithm>
#include <optional>

#include "../../types.hpp"

namespace cache::detail {

template <size_t block_size>
class CacheLine {

private:
  static constexpr size_t BLOCK_SIZE = block_size;
  
  using DataArray = std::array<std::byte, BLOCK_SIZE>;

  uint64_t m_tag = 0;
  bool m_valid = false;
  bool m_dirty = false;
  DataArray m_data{};
  // Для LRU
  uint64_t m_age = 0;

  cache::BlockState m_state = cache::BlockState::INVALID;

public:
  CacheLine() = default;
  

  // Геттеры
  uint64_t get_tag() const { return m_tag; }
  bool get_valid() const { return m_valid; }
  bool get_dirty() const { return m_dirty; }
  DataArray& get_data() const { return m_data; }
  uint64_t get_age() const { return m_age; }
  cache::BlockState get_state() const { return m_state; }

  // Сеттеры
  void set_tag(uint64_t tag) { m_tag = tag; }
  void set_valid(bool valid) { m_valid = valid; }
  void set_dirty(bool dirty) { m_dirty = dirty; }
  void set_data(std::array<std::byte, BLOCK_SIZE> data) { m_data = data; }
  void set_age(uint64_t age) { m_age = age; }
  void set_state(cache::BlockState state) { m_state = state; }

  // Чтение данных (из среза кэш-линии)
  std::optional<std::vector<std::byte>> read(uint64_t offset, size_t N) const {
    // Проверка на валидность данных и не выход из блока
    if (!m_valid || offset + N  > BLOCK_SIZE) {
      return std::nullopt;
    }
    std::vector<std::byte> copy(N);
    std::copy(m_data.begin() + offset, m_data.begin() + offset + N, copy.begin());
    return copy;
  }

  // Запись данных (в срез кэш-линии)
  bool write(uint64_t tag, uint64_t offset, std::span<const std::byte> data) {
    // Проверка границ кэш-линии
    if (offset + data.size() > BLOCK_SIZE) {
      return false;
    }
    // Итератор начала записи в кэш линии с учетом смещения
    auto m_data_start = m_data.begin() + offset;
    // Вставляем переписанные байты в нужное место кэш-линии
    std::copy(data.begin(), data.end(), m_data_start);

    m_valid = true;
    m_dirty = true;
    m_state = cache::BlockState::DIRTY;
    m_tag = tag;
    return true;
  }

  // Загрузка из памяти
  void download(uint64_t tag, const DataArray& data = {}) {
    m_tag = tag;
    m_valid = true;
    m_dirty = false;
    m_data = data;
    m_state = cache::BlockState::VALID;
    m_age = 0;
  }

  // Загрузка в память
  DataArray upload() {
    m_valid = false;
    m_dirty = false;
    m_state = cache::BlockState::INVALID;
    return m_data;
  }
};

} // namespace caache::detail
