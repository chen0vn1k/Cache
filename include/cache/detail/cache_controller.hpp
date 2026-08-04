#pragma once

#include <vector>
#include <string>
#include <iostream>
//#include <algorithm>

#include "cache_core.hpp"
#include "../../interfaces.hpp"

namespace cache::detail {

template <
  typename ReplacementPolicy,
  typename WritePolicy,
  typename AllocationPolicy
>
class CacheController :
  // Метаданные для заведения
  private AllocationPolicy::AllocateMeta
{
private:
  using Core_t = CacheCore<ReplacementPolicy, WritePolicy>;
  using Block_t = typename Core_t::Block_t;
  // Метаданные для заведения
  
  // Доступ контроллера к кэшу
  Core_t m_core;
  // Собственное имя кэша
  std::string m_name{"?"};

  // Вспомогательные фукнции
  
  // Получение ответа от нижнего кэша по иерархии с ожиданием
  template<typename LowerResponse>
  cache::Response wait_response(bool is_write, LowerResponse& lower_res) {
    auto lower_resp = lower_res();
    // Ждем пока не получим ответ
    /*
    if (is_write) {
      while (!std::get<WriteResponse>(lower_resp).success) {}
    }
    else {
      while (!std::get<ReadResponse>(lower_resp).success) {}
    }
    */
    return lower_resp;
  }

  // Выполнение запроса (чтение и запись в одном блокe)
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
      return ReadResponse{block.read(offset, r_req.size), true};
    }
  }
  
  // Обработака cache hit для одного блока
  template<typename LowerRequest, typename LowerResponse>
  cache::Response handle_hit(Block_t& block, const cache::Request& req, const DecodedAddress& decoded, 
                             bool is_write, std::vector<Block_t>& set, LowerRequest& lower_req,
                             LowerResponse lower_res) {

    uint64_t block_address = m_core.get_block_address(decoded.tag, decoded.set_index);
    
    // обновляем метаданные в наборе
    ReplacementPolicy::touch(block, set);
    AllocationPolicy::hit_handle(*this, block_address);

    
    // Обработка записи
    if (is_write) {
      WritePolicy::on_hit(block, true);

      // ======== DEBUG ========
      std::clog << "[" << m_name << "] "
                << "Updated state for rewrited block\n";
      // =======================
      auto response = execute(block, req, decoded.offset, true);

      // отправляем запрос дальше для write_through
      if constexpr (WritePolicy::requires_write_through()) { 
        // ======== DEBUG ========
        std::clog << "[" << m_name << "] "
                  << "[WT] write-through to lower\n";
        // =======================
        lower_req(req);
      }
      wait_response(true, lower_res);
      return response;
    }
    // Обработка чтения
    else {
      WritePolicy::on_hit(block, false);
      return execute(block, req, decoded.offset, false);
    }
  }
  
  // Обработка cache miss для одного блока
  template<typename LowerRequest, typename LowerResponse>
  cache::Response handle_miss(const cache::Request& req, const DecodedAddress& decoded, 
                              bool is_write, std::vector<Block_t>& set, 
                              LowerRequest& lower_req, LowerResponse& lower_res) {

    uint64_t block_address = m_core.get_block_address(decoded.tag, decoded.set_index);

    // Проверям политику заведения
    bool need_allocate = is_write 
                         ? AllocationPolicy::need_write_allocate(*this, block_address)
                         : AllocationPolicy::need_read_allocate(*this, block_address);

    // Если заведение запрещено
    if (!need_allocate) {
      // ======== DEBUG ========
      std::clog << "[" << m_name << "] "
                << "[BYPASS] " << (is_write ? "WRITE" : "READ") << " no allocate, forward to lower\n";
      // =======================
      // Отправляем запрос ниже по иерархии в обход кэша
      lower_req(req);
      return wait_response(is_write, lower_res);
    }
    else {
      // Ищем свободные блоки в наборе
      Block_t* target_block = m_core.find_invalid_block(decoded.set_index);
      // Если нет свободных, освобождаем один из занятых по политике replacement
      if (!target_block) {
        // ======== DEBUG ========
        std::clog << "[" << m_name << "] "
                  << "[FULLSET] " << "no free block in set " << decoded.set_index << "\n";
        // =======================

        target_block = &ReplacementPolicy::select_victim(set);
        
        // ======== DEBUG ========
        std::clog << "[" << m_name << "] "
                  << "[VICTIM] " << "choose victim with tag 0x" << std::hex
                  << target_block->get_tag() << std::dec << "\n";
        // =======================
        // Если старый блок изменен, то переносим перекидываем его ниже по иерархии
        if (WritePolicy::is_dirty(*target_block)) {
          // ======== DEBUG ========
          std::clog << "[" << m_name << "] "
                    << "[DIRTY] victim is dirty, write to the lower\n";
          // =======================

          uint64_t old_addr = m_core.get_block_address(target_block->get_tag(), decoded.set_index);
          lower_req(WriteRequest{old_addr, target_block->upload()});
          // Ожидаем ответа о завершении записи
          wait_response(true, lower_res);
          // ======== DEBUG ========
          std::clog << "[" << m_name << "] "
                    << "[RESPONSE] " << "Write is success\n";
          // =======================
        }
        
      }
      
      // Запрашиваем новый блок из памяти
      
      lower_req(ReadRequest{block_address, m_core.get_block_size()});
      auto lower_resp = wait_response(false, lower_res);

      // Загружаем новый блок в кэш
      target_block->download(decoded.tag, std::get<ReadResponse>(lower_resp).data);
      // ======== DEBUG ========
      std::clog << "[" << m_name << "] [FILL] set=" << decoded.set_index
          << " tag=0x" << std::hex << decoded.tag << std::dec << "\n";
      // =======================
      WritePolicy::on_miss(*target_block, is_write);
      ReplacementPolicy::touch(*target_block, set);


      return execute(*target_block, req, decoded.offset, is_write);
    }
  }

  // Обработка транзакции для одного блока
  template<typename LowerRequest, typename LowerResponse>
  cache::Response process_single_transaction(const cache::Request& req, 
                                      LowerRequest& lower_req, 
                                      LowerResponse& lower_res) 
  {
    // Узнаем какого типа запрос и получаем адресс
    bool is_write = std::holds_alternative<cache::WriteRequest>(req);
    uint64_t address = is_write ? std::get<cache::WriteRequest>(req).address 
                                : std::get<cache::ReadRequest>(req).address;

    // Декодируем адресс и ищем блок в кэше
    auto decoded = m_core.decode_address(address);

    // ======== DEBUG ========
    std::clog << "[" << m_name << "] "
              << "[ACCESS] " << (is_write ? "WR" : "RD")
              << " addr=0x" << std::hex << address << std::dec
              << " set=" << decoded.set_index
              << " tag=0x" << std::hex << decoded.tag << std::dec << "\n";
    // =======================


    Block_t* block = m_core.find_block(decoded.set_index, decoded.tag);
    auto& set = m_core.get_set(decoded.set_index);

    // Ветка cache hit
    if (block) {
      // ======== DEBUG ========
      std::clog << "[" << m_name << "] "
                << "[HIT] " << (is_write ? "WR" : "RD")
                << " set=" << decoded.set_index
                << " tag=0x" << std::hex << decoded.tag << std::dec << "\n";
      // =======================

      return handle_hit(*block, req, decoded, is_write, set, lower_req, lower_res);
    }
    // Ветка cahce miss
    else {
      // ======== DEBUG ========
      std::clog << "[" << m_name << "] "
                << "[MISS] " << (is_write ? "WR" : "RD")
                << " set=" << decoded.set_index
                << " tag=0x" << std::hex << decoded.tag << std::dec << "\n";

      return handle_miss(req, decoded, is_write, set, lower_req, lower_res);
    }
  }

public:
  CacheController(size_t num_sets, size_t associativity, size_t block_size) :
    AllocationPolicy::AllocateMeta{},
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
    const size_t block_size = m_core.get_block_size();
    // Узнаем какого типа запрос и получаем адресс
    bool is_write = std::holds_alternative<cache::WriteRequest>(req);
    
    // Обработка чтения
    if (!is_write) {
      const auto& r_req = std::get<cache::ReadRequest>(req);
      uint64_t current_address = r_req.address;
      size_t bytes_left = r_req.size;

      // Итоговый набор возвращаемых данных
      std::vector<std::byte> result_data;
      result_data.reserve(r_req.size);

      // Цикл нарезки запроса по границам блоков
      while (bytes_left > 0) {
        size_t offset = current_address % block_size;
        size_t chunk_size = std::min(bytes_left, block_size - offset);

        // Создаем подзапрос для текущего блока
        cache::ReadRequest sub_req{current_address, chunk_size};
        auto sub_resp = process_single_transaction(sub_req, lower_req, lower_res);
        
        // Аккумулируем прочитанные байты в один большой ответ
        const auto& read_resp = std::get<cache::ReadResponse>(sub_resp);
        result_data.insert(result_data.end(), read_resp.data.begin(), read_resp.data.end());

        // Двигаемся по адресу, до последнего байта слева
        current_address += chunk_size;
        bytes_left -= chunk_size;
      }

      return cache::ReadResponse{std::move(result_data), true};

    } 
    // Обработка записи
    else {
      const auto& w_req = std::get<cache::WriteRequest>(req);
      uint64_t current_address = w_req.address;
      size_t bytes_left = w_req.data.size();
      size_t data_offset = 0;

      // Цикл нарезки записи по границам блоков
      while (bytes_left > 0) {
        size_t offset = current_address % block_size;
        size_t chunk_size = std::min(bytes_left, block_size - offset);

        // Срез записываемых данных для текущего блока
        std::vector<std::byte> sub_data(
          w_req.data.begin() + data_offset, 
          w_req.data.begin() + data_offset + chunk_size
        );

        cache::WriteRequest sub_req{current_address, std::move(sub_data)};
        process_single_transaction(sub_req, lower_req, lower_res);

        current_address += chunk_size;
        data_offset += chunk_size;
        bytes_left -= chunk_size;
      }

      return cache::WriteResponse{true};
    }

  }

  size_t get_block_size() const noexcept { return m_core.get_block_size(); }
  size_t get_num_sets() const noexcept { return m_core.get_num_sets(); }
  size_t get_associativity() const noexcept { return m_core.get_associativity(); }


  // Определение имени кэша
  void set_name(std::string name) { m_name = std::move(name); }
  // Получение имени кэша
  const std::string& name() const { return m_name; }
};

} // namespace cache
