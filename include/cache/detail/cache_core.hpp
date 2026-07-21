#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <bit>

#include "../../types.hpp"
#include "cache_line.hpp"

namespace cache::detail {

template <cache::CacheConfig cache_config>
class CacheController;

template<cache::CacheConfig cache_config>
class CacheCore{
  // Разрешаем контроллеру с той же конфигурацией полный доступ
  friend class CacheController<cache_config>;
private:
  static constexpr size_t CACHE_SIZE = cache_config.cache_size;
  static constexpr size_t BLOCK_SIZE = cache_config.block_size;
  static constexpr size_t ASSOCIATIVITY = cache_config.associativity;
  static constexpr cache::ReplacementPolicy POLICY = cache_config.replacement_policy;
  static constexpr cache::WritePolicy WRITE_POLICY = cache_config.write_policy;

  // Число наборов в кэше
  static constexpr size_t NUM_SETS = CACHE_SIZE / (BLOCK_SIZE * ASSOCIATIVITY);
  // размеры в битах для адреса
  static constexpr size_t OFFSET_BITS = std::countr_zero(BLOCK_SIZE);
  static constexpr size_t SET_INDEX_BITS = std::countr_zero(NUM_SETS);

  static_assert(std::has_single_bit(BLOCK_SIZE));
  static_assert(std::has_single_bit(NUM_SETS));

  using CacheLine_t = CacheLine<BLOCK_SIZE>;
  using SetArray = std::array<CacheLine_t, ASSOCIATIVITY>;
  using CacheArray = std::array<SetArray, NUM_SETS>;
  
  // Основные переменные кэша
  CacheArray m_cache{};

  // Нужные функции
  
  // Получение смещения внутри блока
  constexpr uint64_t get_offset(uint64_t address) const {
    return address & (static_cast<uint64_t>(BLOCK_SIZE) - 1);
  }
  
  // Получение тега
  constexpr uint64_t get_tag(uint64_t address) const {
    return address >> (OFFSET_BITS + SET_INDEX_BITS);
  }
  
  // Получение индекса множества
  constexpr uint64_t get_set_index(uint64_t address) const {
    return (address >> OFFSET_BITS) & (static_cast<uint64_t>(NUM_SETS) - 1);
  }

  // Восстановление адреса блока
  constexpr uint64_t get_block_address(uint64_t tag, uint64_t set_index){
    return (tag << (OFFSET_BITS + SET_INDEX_BITS)) | (set_index << OFFSET_BITS);
  }
  
  // Поиск линии в наборе
  std::optional<size_t> find_line(size_t set_index, uint64_t tag) const {
    const auto& set = m_cache[set_index];
   // Находим и возвращаем индекс или пустое значение 
    for (size_t way = 0; way < ASSOCIATIVITY; ++way) {
      if (set[way].get_valid() && set[way].get_tag() == tag) {
        return way;
      }
    }
    return std::nullopt;
  }

public:
  CacheCore() = default;
  
  // Чтение данных из кэша (считаем что запрос не выходит за гранциы одной кэш-линии)
  std::optional<std::vector<std::byte>> read(uint64_t address, size_t N) {
    // Вычисляем нужные парамеры по адресу
    uint64_t offset = get_offset(address);
    uint64_t set_index = get_set_index(address);
    uint64_t tag = get_tag(address);
    
    std::optional way_index = find_line(set_index, tag); 
    // проверяем есть ли кэш-линия в наборе
    if (way_index) {
      // Проверяем поподают ли данные в область одного блока  
      const auto& cache_line = m_cache[set_index][*way_index];

      return cache_line.read(offset, N);
    } 
    else {
      return std::nullopt;
    }
  }
  
  // Запись данных в кэш
  bool write(uint64_t address, size_t way_index, std::span<const std::byte> data) {
    uint64_t offset = get_offset(address);
    uint64_t set_index = get_set_index(address);
    uint64_t tag = get_tag(address);
    
    CacheLine_t& cache_line = m_cache[set_index][way_index];
    bool result = cache_line.write(tag, offset, data);
    return result;

    
  }

  // Загрузга из памяти
  void download(uint64_t address, size_t way_index, const std::array<std::byte, BLOCK_SIZE>& data = {}) {
    uint64_t set_index = get_set_index(address);
    uint64_t tag = get_tag(address);

    CacheLine_t& cache_line = m_cache[set_index][way_index];
    cache_line.download(tag, data);
  }
  // Загрузка в память
  std::array<std::byte, BLOCK_SIZE> upload(size_t set_index, size_t way_index) {
    CacheLine_t& cache_line = m_cache[set_index][way_index];
    return cache_line.upload();

  }

};

} // namespace cache::detail
