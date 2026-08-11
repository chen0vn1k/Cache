#pragma once

#include <vector>
#include <string>
#include <iostream>
//#include <algorithm>

#include "cache_core.hpp"
#include "../../interfaces.hpp"
#include "cache_trace.hpp"

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
  // Собственное имя кэша и нижнего по иерархии
  std::string m_name{"?"};
  std::string m_lower_name{"?"};

  // Вспомогательные фукнции
  
  // Получение ответа от нижнего кэша по иерархии с ожиданием
  template<typename LowerResponse>
  cache::Response wait_response(bool is_write, LowerResponse& lower_res) {
    auto lower_resp = lower_res();
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
    
    // =================== TRACE ===================
    if constexpr (log::kEnabled) {
      log::Tracer tr(m_name, m_lower_name);
      const int way = log::way_of(block, set);
      const size_t access_size = is_write
          ? std::get<WriteRequest>(req).data.size()
          : std::get<ReadRequest>(req).size;
      const uint64_t req_addr = is_write
          ? std::get<WriteRequest>(req).address
          : std::get<ReadRequest>(req).address;
      auto snap = block.upload();
      auto slice = log::slice_data(snap, decoded.offset, access_size);
      tr.hit(decoded.set_index, way, decoded.tag, decoded.offset,
             is_write, req_addr, access_size, &slice);
    }
    // =============================================

    // обновляем метаданные в наборе
    ReplacementPolicy::touch(block, set);
    AllocationPolicy::hit_handle(*this, block_address);

    
    // Обработка записи
    if (is_write) {

      // =================== TRACE ===================
      const char* from_state = nullptr;
      if constexpr (log::kEnabled) {
        from_state = log::StateName<WritePolicy>::get(block);
      }
      // =============================================

      WritePolicy::on_hit(block, true);

      auto response = execute(block, req, decoded.offset, true);

      // =================== TRACE ===================
      if constexpr (log::kEnabled) {
        log::Tracer tr(m_name, m_lower_name);
        tr.state(decoded.set_index, log::way_of(block, set), decoded.tag,
                 from_state, log::StateName<WritePolicy>::get(block));
      // =============================================

      }
      // отправляем запрос дальше для write_through
      if constexpr (WritePolicy::requires_write_through()) { 

        // =================== TRACE ===================
        if constexpr (log::kEnabled) {
          log::Tracer tr(m_name, m_lower_name);
          const auto& w = std::get<WriteRequest>(req);
          tr.req_down(true, w.address, w.data.size(), &w.data);
        }
        // =============================================

        lower_req(req);

        // =================== TRACE ===================
        auto resp = wait_response(true, lower_res);
        if constexpr (log::kEnabled) {
          log::Tracer tr(m_name, m_lower_name);
          const auto& w = std::get<WriteRequest>(req);
          tr.resp_up(true, w.address, w.data.size(),
                     std::get<WriteResponse>(resp).success);
        }
        (void)resp;
        // =============================================

      }
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

    // =================== TRACE ===================
    if constexpr (log::kEnabled) {
      log::Tracer tr(m_name, m_lower_name);
      const uint64_t req_addr = is_write
          ? std::get<WriteRequest>(req).address
          : std::get<ReadRequest>(req).address;
      tr.miss(decoded.set_index, decoded.tag, is_write, req_addr);
    }
    // =============================================

    // Проверям политику заведения
    bool need_allocate = is_write 
                         ? AllocationPolicy::need_write_allocate(*this, block_address)
                         : AllocationPolicy::need_read_allocate(*this, block_address);

    // Если заведение запрещено
    if (!need_allocate) {

      // =================== TRACE ===================
      if constexpr (log::kEnabled) {
        log::Tracer tr(m_name, m_lower_name);
        const uint64_t req_addr = is_write
            ? std::get<WriteRequest>(req).address
            : std::get<ReadRequest>(req).address;
        const size_t req_sz = is_write
            ? std::get<WriteRequest>(req).data.size()
            : std::get<ReadRequest>(req).size;
        const auto* wr_data = is_write ? &std::get<WriteRequest>(req).data : nullptr;
        tr.bypass(is_write, req_addr, req_sz, wr_data);
        tr.req_down(is_write, req_addr, req_sz, wr_data);
      }
      // =============================================

      // Отправляем запрос ниже по иерархии в обход кэша
      lower_req(req);
      auto resp = wait_response(is_write, lower_res);

      // =================== TRACE ===================
      if constexpr (log::kEnabled) {
        log::Tracer tr(m_name, m_lower_name);
        const uint64_t req_addr = is_write
            ? std::get<WriteRequest>(req).address
            : std::get<ReadRequest>(req).address;
        const size_t req_sz = is_write
            ? std::get<WriteRequest>(req).data.size()
            : std::get<ReadRequest>(req).size;
        if (!is_write) {
          const auto& rd = std::get<ReadResponse>(resp);
          tr.resp_up(false, req_addr, req_sz, rd.success, &rd.data);
        } else {
          tr.resp_up(true, req_addr, req_sz,
                     std::get<WriteResponse>(resp).success);
        }
      }
      // =============================================
      
      return resp;
    }
    else {
      // Ищем свободные блоки в наборе
      Block_t* target_block = m_core.find_invalid_block(decoded.set_index);
      // Если нет свободных, освобождаем один из занятых по политике replacement
      if (!target_block) {

        target_block = &ReplacementPolicy::select_victim(set);
        
        uint64_t vtag = target_block->get_tag();
        uint64_t vaddr = m_core.get_block_address(vtag, decoded.set_index);
        bool dirty = WritePolicy::is_dirty(*target_block);
        auto vdata = dirty ? target_block->upload() : std::vector<std::byte>{};

        
        // =================== TRACE ===================
        if constexpr (log::kEnabled) {
          log::Tracer tr(m_name, m_lower_name);
          if (!dirty) vdata = target_block->upload();
          tr.evict(decoded.set_index, log::way_of(*target_block, set),
                   vtag, vaddr, dirty, &vdata);
        }
        // =============================================

        // Если старый блок изменен, то переносим перекидываем его ниже по иерархии
        if (dirty) {

          // =================== TRACE ===================
          if constexpr (log::kEnabled) {
            log::Tracer tr(m_name, m_lower_name);
            tr.wb(vaddr, &vdata);
            tr.req_down(true, vaddr, vdata.size(), &vdata);
          }
          // =============================================

          lower_req(WriteRequest{vaddr, target_block->upload()});
          // Ожидаем ответа о завершении записи
          auto resp = wait_response(true, lower_res);

          // =================== TRACE ===================
          if constexpr (log::kEnabled) {
            log::Tracer tr(m_name, m_lower_name);
            tr.resp_up(true, vaddr, 0, std::get<WriteResponse>(resp).success);
          }
          (void)resp;
          // =============================================
        }
        
      }

      // =================== TRACE ===================
      if constexpr (log::kEnabled) {
        log::Tracer tr(m_name, m_lower_name);
        tr.req_down(false, block_address, m_core.get_block_size());
      }
      // =============================================
      
      // Запрашиваем новый блок из памяти
      
      lower_req(ReadRequest{block_address, m_core.get_block_size()});
      auto lower_resp = wait_response(false, lower_res);

      // =================== TRACE ===================
      const char* from_state = nullptr;
      if constexpr (log::kEnabled) {
        log::Tracer tr(m_name, m_lower_name);
        const auto& rd = std::get<ReadResponse>(lower_resp);
        tr.resp_up(false, block_address, rd.data.size(), rd.success, &rd.data);
        tr.alloc(decoded.set_index, log::way_of(*target_block, set), decoded.tag);
        from_state = log::StateName<WritePolicy>::get(*target_block);
      }
      // =============================================

      // Загружаем новый блок в кэш
      target_block->download(decoded.tag, std::get<ReadResponse>(lower_resp).data);
      WritePolicy::on_miss(*target_block, is_write);
      ReplacementPolicy::touch(*target_block, set);


    // =================== TRACE ===================
    if constexpr (log::kEnabled) {
      log::Tracer tr(m_name, m_lower_name);
      const int alloc_way = log::way_of(*target_block, set);
      tr.fill(decoded.set_index, alloc_way, decoded.tag,
              &std::get<ReadResponse>(lower_resp).data);
      tr.state(decoded.set_index, alloc_way, decoded.tag,
               from_state, log::StateName<WritePolicy>::get(*target_block));
    }
    // =============================================

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



    Block_t* block = m_core.find_block(decoded.set_index, decoded.tag);
    auto& set = m_core.get_set(decoded.set_index);

    // Ветка cache hit
    if (block) {

      return handle_hit(*block, req, decoded, is_write, set, lower_req, lower_res);
    }
    // Ветка cahce miss
    else {
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


  void flush() { m_core.flush(); }

  // Определение имени кэша
  void set_name(std::string name) { m_name = std::move(name); }
  // Получение имени кэша
  const std::string& name() const { return m_name; }

  // Определение имени нижнего кэша
  void set_lower_name(std::string name) { m_lower_name = std::move(name); }
  // Получение имени нижнего кэша
  const std::string& lower_name() const { return m_lower_name; }
};

} // namespace cache
