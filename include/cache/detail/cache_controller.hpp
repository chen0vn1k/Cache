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
  cache::Response execute(Block_t& block, const cache::Request& req, size_t offset, bool is_write) {
    // Обработка записи
    if (is_write) {
      const auto& w_req = std::get<cache::WriteRequest>(req);
      block.write(offset, w_req.data);
      return WriteResponse{true};
    }
    // Обработка чтения
    else {
      const auto& r_req = std::get<ReadRequest>(req);
      return block.read(offset, r_req.size);
    }
  }
  
  // Обработака cache hit
  template<typename LowerRequest>
  cache::Response handle_hit(Block_t& block, const cache::Request& req, size_t offset, 
                             bool is_write, std::vector<Block_t>& set, LowerRequest& lower_req) {
    // обновляем метаданные allocation в наборе
    ReplacementPolicy::touch(*block, set);
    
    if (is_write) {
      auto response = execute(block, req, offset, true);
      // отправляем запрос дальше для write_through
      if constexpr (WritePolicy::requires_write_through()) lower_req(req);
      return response;
    }
    else {
      WritePolicy::on_read_hit(block);
      return execute(block, req, offset, false);
    }
  }
  
  // Обработка cache miss
  template<typename LowerRequest, typename LowerResponse>
  cache::Response handle_miss(const cache::Request& req, const DecodedAddress& decoded, 
                              bool is_write, std::vector<Block_t>& set, 
                              LowerRequest& lower_req, LowerResponse& lower_res) {
    // Для no write-allocate
    if (is_write && !AllocationPolicy::write_allocate()) {
      lower_req(req);
      return WriteResponse{true};
    }
    else {
      // ищем свободные блоки в наборе
      Block_t* target_block = m_core.find_invalid_block(decoded.set_index);
      // Если нет свободных, освобождаем один из занятых по политике replacement
      if (!target_block) {
        target_block = &ReplacementPolicy::select_victim(set);
        if (WritePolicy::is_dirty(*target_block)) {
          uint64_t old_addr = m_core.get_block_address(target_block->get_tag(), decoded.set_index);
          // Выгружаем старый блок из памяти кэша
          lower_req(WriteRequest{old_addr, target_block->upload()});
        }
      }
      
      uint64_t block_addr = m_core.get_block_address(decoded.tag, decoded.set_index);
      lower_req(ReadRequest{block_addr, m_core.get_block_size()});
      auto lower_resp = lower_res();
      // Загружаем новый блок в кэш
      target_block->download(decoded.tag, std::get<ReadResponse>(lower_resp).data);
      WritePolicy::on_write_miss(*target_block, is_write);
      ReplacementPolicy::touch(*target_block, set);

      return execute(*target_block, req, decoded.offset, is_write);
    }
    
  }


public:
  CacheController(size_t num_sets, size_t associativity, size_t block_size) :
    m_core(num_sets, associativity, block_size)
  {}

  // Обработка транзакции с интерфейсами
  // lower_req - функция передачи данных в память
  // lower_res функция получения данных из памяти
  template<typename LowerRequest, typename LowerResponse>
  cache::Response process_transaction(const cache::Request& req, 
                                      LowerRequest& lower_req, 
                                      LowerResponse& lower_res) 
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
      return handle_miss(req, decoded, is_write, set, lower_req, lower_res);
    }
  }
};

} // namespace cache
