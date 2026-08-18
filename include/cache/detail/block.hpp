#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <algorithm>

//#include "../../types.hpp"

namespace cache::detail {

template <typename ReplacementPolicy, typename WritePolicy>
class Block : // добавляем метаданные в зависимости от политик
  public ReplacementPolicy::BlockMeta,
  public WritePolicy::BlockMeta
{
private:
  uint64_t m_tag;
  std::vector<std::byte> m_data;

public:
  explicit Block(size_t block_size = 64) :
    ReplacementPolicy::BlockMeta{},
    WritePolicy::BlockMeta{},
    m_tag(0),
    m_data(block_size)
  {} 

  // Поддержка копирования и перемещения (нужна для создания vector в кэше)
    Block(const Block&) = default;
    Block& operator=(const Block&) = default;
    Block(Block&&) noexcept = default;
    Block& operator=(Block&&) noexcept = default;

  // Работа с тэгами
  uint64_t get_tag() const noexcept
  {
    return m_tag;
  }

  void set_tag(uint64_t tag) noexcept
  {
    m_tag = tag;
  }

  // Чтение данных (из среза блока)
  std::vector<std::byte> read(uint64_t offset, size_t N) const
  {
    std::vector<std::byte> copy(N);
    std::copy(m_data.begin() + offset, m_data.begin() + offset + N, copy.begin());
    return copy;
  }

  // Запись данных (в срез блока)
  void write(uint64_t offset, std::span<const std::byte> data)
  {
    // Итератор начала записи в кэш линии с учетом смещения
    auto m_data_start = m_data.begin() + offset;
    // вставляем переписанные байты в нужное место кэш-линии
    std::copy(data.begin(), data.end(), m_data_start);
  }

  // Загрузка из памяти
  void download(uint64_t tag, const std::vector<std::byte>& data)
  {
    m_tag = tag;
    std::copy(data.begin(), data.end(), m_data.begin());
  }

  // Загрузка в память
  std::vector<std::byte> upload() const
  {
    return m_data;
  }

  // сброс блока
  void reset()
  {
    m_tag = 0;
    std::fill(m_data.begin(), m_data.end(), std::byte{0});
    // Сброс метаданных всех политик (даннах связянных с ними)
    static_cast<typename ReplacementPolicy::BlockMeta&>(*this) = typename ReplacementPolicy::BlockMeta{};
    static_cast<typename WritePolicy::BlockMeta&>(*this) = typename WritePolicy::BlockMeta{};
  }
};

} // namespace cache::detail
