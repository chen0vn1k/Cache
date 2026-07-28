#pragma once

#include <vector>
#include <algorithm>

#include "cache_core.hpp"
#include "../../interfaces.hpp"

namespace cache::detail {

template <
  typename ReplacementPolicy,
  typename WritePolicy,
  typename AllocationPolicy
>
class CacheController {
private:
  using Core_t = CacheCore<ReplacementPolicy, WritePolicy, AllocationPolicy>;
  using Block_t = typename Core_t::Block_t;
  // Доступ контроллера к кэшу
  Core_t m_core;

  // Вспомогательные фукнции
  
  // Выполнение запроса (чтение и запись в блок)
  cache::Response execute_io(Block_t& block, const cache::Request& req, size_t offset, bool is_write) {
    // Обработка записи
    if (is_write) {
      const auto& w_req = std::get<cache::WriteRequest>(req);
      if 
    }
  }
  
  // Обработака cache hit
  template<typename LowerRequest>
  cache::Response handle_hit(Block_t& block, const cache::Request& req, size_t offset, 
                             bool is_write, std::vector<Block_t>& set, LowerRequest& lower_req);
  
  // Обработка cache miss
  template<typename LowerRequest, typename LowerResponse>
  cache::Response handle_miss(const cache::Request& req, const DecodedAddress& decoded, 
                              bool is_write, std::vector<Block_t>& set, 
                              LowerRequest& lower_req, LowerResponse& lower_resp);

public:
  CacheController(size_t num_sets, size_t associativity, size_t block_size) :
    m_core(num_sets, associativity, block_size)
  {}

  // Обработка транзакции с интерфейсами
  // to_mem - функция передачи данных в память
  // from_mem функция получения данных из памяти
  template<typename LowerRequest, typename LowerResponse>
  cache::Response process_transaction(const cache::Request& req, 
                                      LowerRequest& lower_req, 
                                      LowerResponse& lower_resp) 
  {
    // Узнаем какого типа запрос и получаем адресс
    bool is_write = std::holds_alternative<cache::WriteRequest>(req);
    uint64_t address = is_write ? std::get<cache::WriteRequest>(req).address 
                                : std::get<cache::ReadRequest>(req).address;

    // Декодируем адресс и ищем блок в кэше
    auto decoded = m_core.decode_address(address);
    Block_t* block = m_core.find_block(decoded.set_index, decoded.tag);

    auto& set = m_core.get_set(decoded.set_index);

    // Ветка cache hit
    if (block) {
      return handle_hit(*block, req, decoded.offset, is_write, set, lower_req);
    }
    // Ветка cahce miss
    else {
      return handle_miss(req, decoded, is_write, set, lower_req, lower_resp);
    }
  }
};

} // namespace cache
