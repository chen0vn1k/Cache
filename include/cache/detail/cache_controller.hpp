#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <ostream>
#include <iomanip>

#include "policies.hpp"
#include "cache_core.hpp"
#include "../../interfaces.hpp"
#include "trace_logger.hpp"

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
                                      LowerResponse& lower_res);

  // Получить размер блока
  size_t get_block_size() const noexcept
  {
    return m_core.get_block_size();
  }

  // Получить размер набора
  size_t get_num_sets() const noexcept
  {
    return m_core.get_num_sets();
  }

  // Получить ассоциативность
  size_t get_associativity() const noexcept
  {
    return m_core.get_associativity();
  }

  // Определение имени кэша
  void set_name(std::string name)
  {
    m_name = std::move(name);
  }

  // Получение имени кэша
  const std::string& name() const
  {
    return m_name;
  }

  // Определение имени нижнего кэша
  void set_lower_name(std::string name)
  {
    m_lower_name = std::move(name);
  }

  // Получение имени нижнего кэша
  const std::string& lower_name() const
  {
    return m_lower_name;
  }

  // Снимок валидных блоков: ключ = block address
  // Формат строки: key=0xADDR set=.. way=.. tag=0x.. meta.. data=..
  void dump(std::ostream& out) const
  {
    for (size_t s = 0; s < m_core.get_num_sets(); ++s)
    {
      const auto& set = m_core.get_set(s);
      for (size_t w = 0; w < set.size(); ++w)
      {
        const auto& block = set[w];
        if (!WritePolicy::is_valid(block))
        {
          continue;
        }
        const uint64_t key = m_core.get_block_address(block.get_tag(), s);
        const auto meta = meta_of(block);
        const auto data = block.upload();

        out << std::hex << "0x" << key << std::dec
            << " set=" << s
            << " way=" << w
            << " tag=0x" << std::hex << block.get_tag() << std::dec;
        for (const auto& [k, v] : meta)
        {
          out << ' ' << k << '=' << v;
        }
        out << " data=";
        // те же 4-байтные слова, без хвостовых нулевых слов
        {
          bool any = false;
          size_t last_nz = data.size();
          while (last_nz > 0 && data[last_nz - 1] == std::byte{0})
          {
            --last_nz;
          }
          size_t first_nz = 0;
          while (first_nz < last_nz && data[first_nz] == std::byte{0})
          {
            ++first_nz;
          }
          // выравниваем first к границе слова 4
          first_nz = (first_nz / 4) * 4;
          if (last_nz == 0)
          {
            out << "0";
          }
          else
          {
            for (size_t i = first_nz; i < last_nz; i += 4)
            {
              if (any) out << '\'';
              any = true;
              uint32_t word = 0;
              for (size_t j = 0; j < 4 && (i + j) < data.size(); ++j)
              {
                word |= (static_cast<uint32_t>(data[i + j]) << ((3 - j) * 8));
              }
              out << std::hex << std::setw(8) << std::setfill('0') << word << std::dec;
            }
          }
        }
        out << '\n';
      }
    }
  }

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
  
  // Индекс way в наборе
  static int way_of(const Block_t& block, const std::vector<Block_t>& set)
  {
    return static_cast<int>(&block - set.data());
  }

 // Собрать метаданные политик для трассировки
  static std::map<std::string, std::string> meta_of(const Block_t& block)
  {
    return cache::collect_metadata<ReplacementPolicy, WritePolicy>(block);
  }

  // Получение ответа от нижнего кэша по иерархии с ожиданием
  template<typename LowerResponse>
  cache::Response wait_response(bool is_write, LowerResponse& lower_res)
  {
    auto lower_resp = lower_res();
    return lower_resp;
  }

  // Выполнение запроса (чтение и запись в одном блокe)
  cache::Response execute(Block_t& block, const cache::Request& req, bool is_write,
                          const DecodedAddress& decoded, std::vector<Block_t>& set)
  {

    auto& log = cache::log::TraceLogger::instance();

    const int way = way_of(block, set);

    auto pdata = block.upload();

    // Обработка записи
    if (is_write)
    {
      const auto& w_req = std::get<cache::WriteRequest>(req);
      block.write(decoded.offset, w_req.data);
      
      // =================== TRACE ===================
      log.write(m_name, w_req.address,
              decoded.set_index, decoded.tag,
              way, decoded.offset, meta_of(block), w_req.data);
      // =============================================
      return WriteResponse{true};
    }
    // Обработка чтения
    else
    {
      const auto& r_req = std::get<ReadRequest>(req);
      auto r_data = block.read(decoded.offset, r_req.size);
      // =================== TRACE ===================
      log.read(m_name, r_req.address,
              decoded.set_index, decoded.tag, way,
              decoded.offset, meta_of(block), r_data);
      // =============================================
      return ReadResponse{r_data, true};
    }
  }
  
  // Обработака cache hit для одного блока
  template<typename LowerRequest, typename LowerResponse>
  cache::Response handle_hit(Block_t& block, const cache::Request& req, const DecodedAddress& decoded, 
                             bool is_write, std::vector<Block_t>& set, LowerRequest& lower_req,
                             LowerResponse lower_res)
  {

    uint64_t block_address = m_core.get_block_address(decoded.tag, decoded.set_index);
    
    // =================== TRACE ===================
    auto& log = cache::log::TraceLogger::instance();

    const uint64_t req_addr = is_write
    ? std::get<WriteRequest>(req).address
    : std::get<ReadRequest>(req).address;

    const int way = way_of(block, set);

    auto pdata = block.upload();

    log.hit(m_name, is_write, req_addr,
            decoded.set_index, decoded.tag,
            way, meta_of(block), pdata);

    // =============================================

    // обновляем метаданные в наборе
    AllocationPolicy::hit_handle(*this, block_address);

    
    // Обработка записи
    if (is_write)
    {

      WritePolicy::on_hit(block, true);
      ReplacementPolicy::touch(block, set);

      auto response = execute(block, req, true, decoded, set);

      // =================== TRACE ===================
      log.update(m_name, true, req_addr, decoded.set_index, decoded.tag,
                 way, meta_of(block), block.upload());
      // =============================================

      // отправляем запрос дальше для write_through
      if constexpr (WritePolicy::requires_write_through())
      { 
        lower_req(req);

        // =================== TRACE ===================
        auto resp = wait_response(true, lower_res);
        auto resp_data = is_write ? std::get<WriteRequest>(req).data
                           : std::get<ReadResponse>(resp).data;
        log.response(m_lower_name, m_name, true, req_addr,
                     resp_data);
        (void)resp;
        // =============================================

      }
      return response;
    }
    // Обработка чтения
    else
    {
      WritePolicy::on_hit(block, false);
      ReplacementPolicy::touch(block, set);
      auto ex = execute(block, req, false, decoded, set);
      log.update(m_name, false, req_addr, decoded.set_index, decoded.tag,
                 way, meta_of(block), block.upload());
      return ex;
    }
  }
  
  // Обработка cache miss для одного блока
  template<typename LowerRequest, typename LowerResponse>
  cache::Response handle_miss(const cache::Request& req, const DecodedAddress& decoded, 
                              bool is_write, std::vector<Block_t>& set, 
                              LowerRequest& lower_req, LowerResponse& lower_res)
  {
    auto& log = cache::log::TraceLogger::instance();

    uint64_t block_address = m_core.get_block_address(decoded.tag, decoded.set_index);

    // =================== TRACE ===================
    const uint64_t req_addr = is_write
        ? std::get<WriteRequest>(req).address
        : std::get<ReadRequest>(req).address;

    log.miss(m_name, is_write, req_addr, decoded.set_index, decoded.tag);
    // =============================================

    // Проверям политику заведения
    bool need_allocate = is_write 
                         ? AllocationPolicy::need_write_allocate(*this, block_address)
                         : AllocationPolicy::need_read_allocate(*this, block_address);

    // Если заведение запрещено
    if (!need_allocate)
    {
      // =================== TRACE ===================
      log.request(m_name, m_lower_name, req);
      // =============================================

      // Отправляем запрос ниже по иерархии в обход кэша
      lower_req(req);
      auto resp = wait_response(is_write, lower_res);

      // =================== TRACE ===================
      auto resp_data = is_write ? std::get<WriteRequest>(req).data
                           : std::get<ReadResponse>(resp).data;

      log.response(m_lower_name, m_name, is_write, req_addr, resp_data);
      // =============================================
      
      return resp;
    }
    else
    {
      // Ищем свободные блоки в наборе
      Block_t* target_block = m_core.find_invalid_block(decoded.set_index);
      // Если нет свободных, освобождаем один из занятых по политике replacement
      if (!target_block)
      {

        target_block = &ReplacementPolicy::select_victim(set);
        
        uint64_t vtag = target_block->get_tag();
        uint64_t vaddr = m_core.get_block_address(vtag, decoded.set_index);
        bool dirty = WritePolicy::is_dirty(*target_block);
        auto vdata = dirty ? target_block->upload() : std::vector<std::byte>{};

        
        // =================== TRACE ===================
        log.evict(m_name, is_write, vaddr,
                  decoded.set_index, vtag, way_of(*target_block, set),
                  meta_of(*target_block), vdata);
        // =============================================

        // Если старый блок изменен, то переносим перекидываем его ниже по иерархии
        if (dirty)
        {
          cache::WriteRequest wb_req{vaddr, target_block->upload()};
          cache::Request wb_as_req = wb_req;

          // =================== TRACE ===================
          log.request(m_name, m_lower_name, wb_as_req);
          // =============================================

          lower_req(wb_req);
          // Ожидаем ответа о завершении записи
          auto resp = wait_response(true, lower_res);

          // =================== TRACE ===================
          log.response(m_lower_name, m_name, true, vaddr, wb_req.data);
          // =============================================

          (void) resp;
        }
      }

      // =================== TRACE ===================

      // =============================================
      
      // Запрашиваем новый блок из памяти
      
      lower_req(ReadRequest{block_address, m_core.get_block_size()});

      // =================== TRACE ===================
      cache::ReadRequest fill_req{block_address, m_core.get_block_size()};
      cache::Request fill_as_req = fill_req;

      log.request(m_name, m_lower_name, fill_as_req);
      // =============================================

      auto lower_resp = wait_response(false, lower_res);

      // =================== TRACE ===================
      const auto& rd = std::get<ReadResponse>(lower_resp);
      log.response(m_lower_name, m_name, false, block_address, rd.data);
      // =============================================

      // Загружаем новый блок в кэш
      target_block->download(decoded.tag, std::get<ReadResponse>(lower_resp).data);
      const int alloc_way = way_of(*target_block, set);


      log.fill(m_name, is_write, block_address,
               decoded.set_index, decoded.tag, alloc_way,
               meta_of(*target_block),
               rd.data);

      WritePolicy::on_miss(*target_block, is_write);
      ReplacementPolicy::touch(*target_block, set);


      auto ex = execute(*target_block, req, is_write, decoded, set);
      log.update(m_name, is_write, req_addr, decoded.set_index, decoded.tag, alloc_way,
                 meta_of(*target_block), target_block->upload());
      return ex;
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
    if (block)
    {
      return handle_hit(*block, req, decoded, is_write, set, lower_req, lower_res);
    }
    // Ветка cahce miss
    else
    {
      return handle_miss(req, decoded, is_write, set, lower_req, lower_res);
    }
  }


};


// ================ Определение больших методов ==============
template <typename RP, typename WP, typename AP>
template<typename LowerRequest, typename LowerResponse>
cache::Response CacheController<RP, WP, AP>::process_transaction(const cache::Request& req, 
                                                                 LowerRequest & lower_req, 
                                                                 LowerResponse& lower_res) 
{
  const size_t block_size = m_core.get_block_size();
  // Узнаем какого типа запрос и получаем адресс
  bool is_write = std::holds_alternative<cache::WriteRequest>(req);
  
  // Обработка чтения
  if (!is_write)
  {
    const auto& r_req = std::get<cache::ReadRequest>(req);
    uint64_t current_address = r_req.address;
    size_t bytes_left = r_req.size;

    // Итоговый набор возвращаемых данных
    std::vector<std::byte> result_data;
    result_data.reserve(r_req.size);

    // Цикл нарезки запроса по границам блоков
    while (bytes_left > 0)
    {
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
  else
  {
    const auto& w_req = std::get<cache::WriteRequest>(req);
    uint64_t current_address = w_req.address;
    size_t bytes_left = w_req.data.size();
    size_t data_offset = 0;

    // Цикл нарезки записи по границам блоков
    while (bytes_left > 0)
    {
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
} // namespace cache
