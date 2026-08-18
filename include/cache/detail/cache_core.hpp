#pragma once

#include <cstddef>
//#include <optional>
//#include <span>
#include <bit>

//#include "../../types.hpp"
#include "block.hpp"


namespace cache::detail {

// Структурное представление разложенного адреса
struct DecodedAddress
{
  uint64_t tag;
  uint64_t set_index;
  uint64_t offset;
};

// Просто объявление класса из другого файла
template <
  typename ReplacementPolicy,
  typename WritePolicy,
  typename AllocationPolicy
>
class CacheController;

// Класс структуры кэша
template<typename ReplacementPolicy, typename WritePolicy>
class CacheCore
{
public:
  using Block_t = Block<ReplacementPolicy, WritePolicy>;
  using SetVector = std::vector<Block_t>;
  using CacheVector = std::vector<SetVector>;

  // Конструктор
  CacheCore(size_t num_sets, size_t associativity, size_t block_size) :
    m_num_sets(num_sets),
    m_associativity(associativity),
    m_block_size(block_size),
    m_offset_bits(std::countr_zero(block_size)),
    m_set_bits(std::countr_zero(m_num_sets)),
    m_cache(num_sets, std::vector<Block_t>(associativity, Block_t(block_size)))
  {}

  // Вернуть число наборов в кэше
  size_t get_num_sets() const noexcept
  {
    return m_num_sets;
  }

  // Вернуть ассоциотивность в кэше
  size_t get_associativity() const noexcept
  {
    return m_associativity;
  }

  // Вернуть размер блока в кэше
  size_t get_block_size() const noexcept
  {
    return m_block_size;
  }
  
  // Вернуть число бит адреса блока
  size_t get_offset_bits() const noexcept
  {
    return m_offset_bits;
  }

  // Вернуть число бит адреса набора
  size_t get_set_bits() const noexcept
  {
    return m_set_bits;
  }

  // Доступ к набору
  SetVector& get_set(size_t set_index)
  {
    return m_cache[set_index];
  }

  // Доступ к набору
  const SetVector& get_set(size_t set_index) const
  {
    return m_cache[set_index];
  }


  // Доступ к блоку
  Block_t& get_block(size_t set_index, size_t way_index)
  {
    return m_cache[set_index][way_index];
  }

  // Доступ к блоку
  const Block_t& get_block(size_t set_index, size_t way_index) const
  {
    return m_cache[set_index][way_index];
  }

  // Восстановление адреса блока
  uint64_t get_block_address(uint64_t tag, uint64_t set_index)
  {
    return (tag << (m_offset_bits + m_set_bits)) | (set_index << m_offset_bits);
  }
  // Восстановление адреса блока
  uint64_t get_block_address(uint64_t tag, uint64_t set_index) const
  {
    return (tag << (m_offset_bits + m_set_bits)) | (set_index << m_offset_bits);
  }


  // Разбирает 64-битный адрес на tag, set_index и offset
  DecodedAddress decode_address(uint64_t address) const noexcept;

  // Поиск блока в наборе по тэгу (возвращаем указатель на блок)
  const Block_t* find_block(size_t set_index, uint64_t tag) const;

  // Поиск блока в наборе по тэгу
  Block_t* find_block(size_t set_index, uint64_t tag);

  // Поиск свободного блока в наборе
  Block_t* find_invalid_block(size_t set_index) noexcept;

private:
  // Основные переменные кэша
  size_t m_num_sets;
  size_t m_associativity;
  size_t m_block_size;
  // Маски и сдвиги для быстрого декодирования адреса
  size_t m_offset_bits;
  size_t m_set_bits;

  CacheVector m_cache{};
};

// ========== Определение больших методов ===============

template<typename ReplacementPolicy, typename WritePolicy>
DecodedAddress CacheCore<ReplacementPolicy, WritePolicy>::decode_address(uint64_t address) const noexcept
{
  DecodedAddress decoded;
  decoded.offset = address & (static_cast<uint64_t>(m_block_size) - 1);
  decoded.set_index = (address >> m_offset_bits) & (static_cast<uint64_t>(m_num_sets) - 1);
  decoded.tag = address >> (m_offset_bits + m_set_bits);
  return decoded;
}

template<typename ReplacementPolicy, typename WritePolicy>
const typename CacheCore<ReplacementPolicy, WritePolicy>::Block_t*
CacheCore<ReplacementPolicy, WritePolicy>::find_block(size_t set_index, uint64_t tag) const
{
  auto& set = m_cache[set_index];
  // Находим и возвращаем индекс или пустое значение 
  for (auto& block : set)
  {
    if (WritePolicy::is_valid(block) && block.get_tag() == tag)
    {
      return &block;
    }
  }
  return nullptr;
}

template<typename ReplacementPolicy, typename WritePolicy>
typename CacheCore<ReplacementPolicy, WritePolicy>::Block_t*
CacheCore<ReplacementPolicy, WritePolicy>::find_block(size_t set_index, uint64_t tag)
{
  auto& set = m_cache[set_index];
  for (auto& block : set)
  {
    if (WritePolicy::is_valid(block) && block.get_tag() == tag)
    {
      return &block;
    }
  }
  return nullptr;
}

template<typename ReplacementPolicy, typename WritePolicy>
typename CacheCore<ReplacementPolicy, WritePolicy>::Block_t*
CacheCore<ReplacementPolicy, WritePolicy>::find_invalid_block(size_t set_index) noexcept
{
  for (auto& block : m_cache[set_index])
  {
    if (!WritePolicy::is_valid(block))
    {
      return &block;
    }
  }
  return nullptr;
}

} // namespace cache::detail
