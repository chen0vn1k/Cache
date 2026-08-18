#pragma once


#include <cstdint>
#include <cstddef>
#include <string_view>
#include <vector>
#include <map>

#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/expressions.hpp>

#include "../../interfaces.hpp"
namespace cache::log
{

// Данные для логирования
struct TraceInfo
{

  // Имена кэшей и тип операции
  std::string_view name = "?";
  std::string_view event = "?";
  std::string_view operation = "?";
  
  uint64_t address = 0;


  size_t set = 0;
  uint64_t tag = 0;
  int way = 0;
  uint64_t offset = 0;


  // Метаданные политик
  std::map<std::string, std::string> metadata = {};

  // данные запроса или блока
  std::vector<std::byte> data {};

  uint64_t size = 0;
};


class TraceLogger
{
public:
  // Инициализация логгера
  static void init();

  // Глобальный экземпляр
  static TraceLogger& instance(); 

  // Общий способ вывода
  void emit(const TraceInfo& info);

  // Логирование запроса
  void request(std::string_view from, std::string_view to, Request req);

  // Логирование ответа
  void response(std::string_view from, std::string_view to, bool is_write,
                uint64_t address, const std::vector<std::byte>& data);

  // Логирование попадания в кэш
  void hit(std::string_view cache, bool is_write, uint64_t address,
           size_t set, uint64_t tag, int way,
           const std::map<std::string, std::string>& metadata,
           const std::vector<std::byte>& data);

  // Логирование промаха в кэш
  void miss(std::string_view cache, bool is_write, uint64_t address,
            size_t set, uint64_t tag);

  // Логирвание найденной жертвы замещения
  void evict(std::string_view cache, bool is_write, uint64_t address,
             size_t set, uint64_t tag, int way,
             const std::map<std::string, std::string>& metadata,
             const std::vector<std::byte>& data);

  // Логирвоание заполнения блока кэша
  void fill(std::string_view cache, bool is_write, uint64_t address,
            size_t set, uint64_t tag, int way,
            const std::map<std::string, std::string>& metadata,
            const std::vector<std::byte>& data);

  // Логирование записи в блок кэша
  void write(std::string_view cache, uint64_t address,
             size_t set, uint64_t tag, int way, size_t offset,
             const std::map<std::string, std::string>& metadata,
             const std::vector<std::byte>& data);

  // Логирование чтения из блока кэша
  void read(std::string_view cache, uint64_t address,
            size_t set, uint64_t tag, int way, size_t offset,
            const std::map<std::string, std::string>& metadata,
            const std::vector<std::byte>& data);

  // Логирование обновления состояний метаданных
  void update(std::string_view cache, bool is_write, uint64_t address,
              size_t set, uint64_t tag, int way,
              const std::map<std::string, std::string>& metadata,
              const std::vector<std::byte>& data);

private:
  TraceLogger() = default;

  // Объявляем logger как член класса
  boost::log::sources::logger_mt m_logger;

};

} // namespace cache::log


