#pragma once

#include <vector>
#include <algorithm>

#include "cache_core.hpp"
#include "../../interfaces.hpp"

namespace cache::detail {

template <cache::CacheConfig cache_config>
class CacheController {
private:
  // Доступ контроллера к кэшу
  CacheCore<cache_config> m_core;

  // Поиск адреса для замещения
  size_t choose_victim(size_t set_index) {
    // Если есть пустой блок, то возвращаем его
    for (size_t way = 0; way < cache_config.associativity; ++way) {
      if (!m_core.m_cache[set_index][way].get_valid()) {
        return way;
      }
    }
    
    // LRU (Надо будет сделать нормально)
    size_t victim_way = 0;
    uint64_t max_age = 0;
    for (size_t way = 0; way < cache_config.associativity; ++way) {
      if (m_core.m_cache[set_index][way].get_age() > max_age) {
        max_age = m_core.m_cache[set_index][way].get_age();
        victim_way = way;
      }
    }
    return victim_way;
  }

  // повышение возраста всех блоков кроме замещенного (его 0)
  void update_ages(size_t set_index, size_t accessed_way) {
    for (size_t way = 0; way < cache_config.associativity; ++way) {
      auto& line = m_core.m_cache[set_index][way];
      if (line.get_valid()) {
        if (way == accessed_way) line.set_age(0);
        line.set_age(line.get_age() + 1);
      }
    }
  }

public:
  CacheController() = default;

  // Обработка транзакции с шинами
  // to_mem - функция передачи данных в память
  // from_mem функция получения данных из памяти
  template<typename MemRequestBus, typename MemResponseBus>
  cache::CpuResponse process_transaction(const cache::CpuRequest& cpu_req, 
                                         MemRequestBus& to_mem, 
                                         MemResponseBus& from_mem) 
  {
    uint64_t set_index = m_core.get_set_index(cpu_req.address);
    uint64_t tag = m_core.get_tag(cpu_req.address);
    
    auto hit_way = m_core.find_line(set_index, tag);
    
    // cache hit
    if (hit_way) {
      update_ages(set_index, *hit_way);

      // Для чтения 
      if (cpu_req.op_type == cache::OperationType::READ) {
        auto data = m_core.read(cpu_req.address, cpu_req.data.size());
        return {true, data.value_or(std::vector<std::byte>{})};
      } 
      // Для записи
      else if (cpu_req.op_type == cache::OperationType::WRITE) {
        m_core.write(cpu_req.address, *hit_way, cpu_req.data);

        // для Write-Through (пока не надо) еще пишем в память
        //if constexpr (cache_config.write_policy == cache::WritePolicy::WRITE_THROUGH) {
          // Запись в память 
        //  to_mem(cache::MemRequest{cpu_req.address, cache::OperationType::WRITE, cpu_req.data});
        //}
        return {true, {}};
      }
    }

    // cache miss
    // нахождение замещаемго блока
    size_t victim_way = choose_victim(set_index);
    auto& victim_block = m_core.m_cache[set_index][victim_way];

    // 1. Вытеснение грязной строки (Write-Back)
    uint64_t victim_tag = victim_block.get_tag();
    uint64_t victim_addr = m_core.get_block_address(victim_tag, set_index);

    if (victim_block.get_valid() && victim_block.get_dirty()) {
      auto raw_data = m_core.upload(set_index, victim_way);
      std::vector<std::byte> evict_data(raw_data.begin(), raw_data.end());
      
      // Запись в память
      to_mem(cache::MemRequest{victim_addr, cache::OperationType::WRITE, evict_data});
    }

    // 2. Перетаскиваем данные из памяти в кэш
    uint64_t block_addr = m_core.get_block_address(tag, set_index);

    to_mem(cache::MemRequest{block_addr, cache::OperationType::READ, {}});
    
    // Данные которые пришли из памяти
    cache::MemResponse mem_res = from_mem(); 

    std::array<std::byte, cache_config.block_size> aligned_data{};
    std::copy(mem_res.data.begin(), mem_res.data.end(), aligned_data.begin());

    // Загружаем данные в кэш
    m_core.download(cpu_req.address, victim_way, aligned_data);
    update_ages(set_index, victim_way);

    // 3. Выполняем операцию CPU

    // Для операции чтения при промахе
    if (cpu_req.op_type == cache::OperationType::READ){
      auto data = m_core.read(cpu_req.address, cpu_req.data.size());
      return {false, data.value_or(std::vector<std::byte>{})};
    }
    // Для операции записи при промахе
    else {
      m_core.write(cpu_req.address, victim_way, cpu_req.data);
      return {false, {}};
    }
  }
};

} // namespace cache
