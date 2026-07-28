#pragma once

#include <cstddef>
//#include <optional>
//#include <span>
#include <bit>

//#include "../../types.hpp"
#include "block.hpp"


namespace cache::detail {

// Структурное представление разложенного адреса
struct DecodedAddress {
    uint64_t tag;
    uint64_t set_index;
    uint64_t offset;
};

template <
  typename ReplacementPolicy,
  typename WritePolicy,
  typename AllocationPolicy
>
class CacheController;

template<
  typename ReplacementPolicy,
  typename WritePolicy,
  typename AllocationPolicy
>
class CacheCore{
  // Разрешаем контроллеру с той же конфигурацией полный доступ
  //friend class CacheController<cache_config>;
public:
  using Block_t = Block<ReplacementPolicy, WritePolicy, AllocationPolicy>;
  using SetVector = std::vector<Block_t>;
  using CacheVector = std::vector<SetVector>;

private:
  // Основные переменные кэша
  size_t m_num_sets;
  size_t m_associativity;
  size_t m_block_size;
  CacheVector m_cache{};

  // Маски и сдвиги для быстрого декодирования адреса
  size_t m_offset_bits = std::countr_zero(m_block_size);
  size_t m_set_bits = std::countr_zero(m_num_sets);

  // Вспомогательная функция проверки, является ли число сх

public:
  CacheCore(size_t num_sets, size_t associativity, size_t block_size) :
    m_num_sets(num_sets),
    m_associativity(associativity),
    m_block_size(block_size),
    m_cache(num_sets, std::vector<Block_t>(associativity, Block_t(block_size)))
  {}

  // Геттеры
  size_t get_num_sets() const noexcept { return m_num_sets; }
  size_t get_associativity() const noexcept { return m_associativity; }
  size_t get_block_size() const noexcept { return m_block_size; }
  size_t get_offset_bits() const noexcept {return m_offset_bits; }
  size_t get_set_bits() const noexcept {return m_set_bits; }
  // Доступ к набору
  SetVector& get_set(size_t set_index) { return m_cache[set_index]; }
  // Доступ к блоку
  Block_t& get_block(size_t set_index, size_t way_index) { return m_cache[set_index][way_index]; }
  // Восстановление адреса блока
  uint64_t get_block_address(uint64_t tag, uint64_t set_index) {
    return (tag << (m_offset_bits + m_set_bits)) | (set_index << m_offset_bits);
  }

  // Разбирает 64-битный адрес на tag, set_index и offset
  DecodedAddress decode_address(uint64_t address) const noexcept {
    DecodedAddress decoded;
    decoded.offset = address & (static_cast<uint64_t>(m_block_size) - 1);
    decoded.set_index = (address >> m_offset_bits) & (static_cast<uint64_t>(m_num_sets) - 1);
    decoded.tag = address >> (m_offset_bits + m_set_bits);
    return decoded;
  }

  // Поиск блока в наборе по тэгу (возвращаем указатель на блок)
  const Block_t* find_block(size_t set_index, uint64_t tag) const {
    auto& set = m_cache[set_index];
    // Находим и возвращаем индекс или пустое значение 
    for (auto& block : set) {
      if (WritePolicy::is_valid(block) && block.get_tag() == tag) {
        return &block;
      }
    }
    return nullptr;
  }

  Block_t* find_block(size_t set_index, uint64_t tag) {
    auto& set = m_cache[set_index];
    for (auto& block : set) {
      if (WritePolicy::is_valid(block) && block.get_tag() == tag) {
        return &block;
      }
    }
    return nullptr;
  }

  // Поиск свободного блока в наборе
  Block_t* find_invalid_block(size_t set_index) noexcept {
      for (auto& block : m_cache[set_index]) {
          if (!WritePolicy::is_valid(block)) return &block;
      }
      return nullptr;
  }
};

} // namespace cache::detail
