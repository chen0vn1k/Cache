#pragma once

#include <optional>

#include "types.hpp"
#include "interfaces.hpp"
#include "cache/detail/cache_controller.hpp"

namespace cache {

template <CacheConfig config>
class CacheSystem {
private:
  detail::CacheController<config> m_controller;

  // Буферы для хранения состояния
  std::optional<CpuRequest> m_cpu_request;
  std::optional<CpuResponse> m_cpu_response;


public:
  CacheSystem() = default;

  // Запрос от CPU к кэшу
  void send_cpu_request(const CpuRequest& req) {
    m_cpu_request = req;
  }

  // Ответ от кэша к CPU
  std::optional<CpuResponse> get_cpu_response() {
    std::optional<CpuResponse> resp = m_cpu_response;
    m_cpu_response = std::nullopt; // Очищаем буфер после прочтения
    return resp;
  }

  // Выполнение запроса от CPU (также подключается функции запроса и ответа от памяти)
  template <typename MemRequestBus, typename MemResponseBus>
  void execute(MemRequestBus to_mem, MemResponseBus from_mem) {
    if (m_cpu_request.has_value()) {
      
      // Передаем запрос и функции памяти в контроллер
      m_cpu_response = m_controller.process_transaction(
          m_cpu_request.value(), 
          to_mem, 
          from_mem
      );

      // После выполнения запроса очищаем буфер
      m_cpu_request = std::nullopt;
    }
  }
};

} // namespace cache
