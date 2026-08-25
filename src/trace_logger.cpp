#include <iomanip>

#include"../include/cache/detail/trace_logger.hpp"
#include"../include/interfaces.hpp"

//#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <boost/log/attributes.hpp>


namespace logging = boost::log;
namespace expr = boost::log::expressions;

namespace cache::log
{
// Вывод данных нужных образом
std::string string_data(const std::vector<std::byte>& data)
{
  if (data.empty())
  {
    return {};
  }
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    size_t size = data.size();
    for (size_t i = 0; i < size; i += 4)
    {
        if (i != 0) {
            oss << '\'';
        }

        // Запоминаем позицию в потоке перед началом записи текущего 4-байтового слова
        std::string word;
        for (size_t j = 0; j < 4 && (i + j) < size; ++j)
        {
            // Используем промежуточную строку
            unsigned int val = std::to_integer<unsigned int>(data[size - 1 -i - j]);
            
            oss << std::setw(2) << val;
        }
    } 

    return oss.str();
}

// Форматирование метаданных политик 
std::string string_metadata( const std::map<std::string, std::string>& metadata)
{
  if (metadata.empty())
  {
    return {};
  }

  // Закидываем сначала в буфер, а потом склеиваем строчку
  std::ostringstream oss;
  bool first = true;
  for (const auto& [name, value] : metadata)
  {
    if (!first)
    {
      oss << ' ';
    }
    first = false;
    oss << name << '=' << value;
  }
  return oss.str();
}

//Подключение трассировки
void TraceLogger::init()
{
  auto sink = logging::add_console_log(
    std::clog,
    logging::keywords::format = 
      expr::stream
        << "[" << expr::attr<std::string>("Cache") << "] "
        << "{" << expr::attr<std::string>("Event") << "} "
        << expr::attr<std::string>("Operation") << " "
        << expr::smessage
  );
}

TraceLogger& TraceLogger::instance()
{
  static TraceLogger logger;
  return logger;
}

// Общий способ вывода
void TraceLogger::emit(const TraceInfo& info)
{
  std::ostringstream msg;

  // addr=
  if (info.address.has_value())
  {
    msg << std::showbase << "addr=" << std::hex << info.address.value() << std::dec << std::noshowbase;
  }

  // size=  (только для RD в REQ)
  if (info.event == "REQ" && info.operation == "RD" && info.size != 0)
  {
    msg << " size=" << info.size;
  }

  // set= tag= way= offset=
  if (info.set.has_value())
  {
    msg << " set=" << info.set.value();
  }
  if (info.tag.has_value())
  {
    msg << std::showbase << " tag=" << std::hex << info.tag.value() << std::dec << std::noshowbase;
  }
  if (info.way.has_value())
  {
    msg << " way=" << info.way.value();
  }
  if (info.offset.has_value())
  {
    msg << " offset=" << info.offset.value();
  }


  // metadata (age=..., frequency=..., ...)
  const auto meta_str = string_metadata(info.metadata);
  if (!meta_str.empty())
  {
    msg << ' ' << meta_str;
  }

  // data=  (для WR всегда, для RD только если есть полезные данные)
  if (!info.data.empty())
  {
    msg << " data=" << string_data(info.data);
  }

  if (!info.other.empty())
  {
    msg << info.other;
  }

  BOOST_LOG(m_logger)
    << logging::add_value("Cache",     std::string(info.name))
    << logging::add_value("Event",     std::string(info.event))
    << logging::add_value("Operation", std::string(info.operation))
    << msg.str();
}

// Логирование запроса
void TraceLogger::request(std::string_view from, std::string_view to, Request req)
{
  // cache_name живёт до конца функции — string_view в TraceInfo безопасен
  const std::string cache_name = std::string(from) + "->" + std::string(to);

  TraceInfo info;
  info.name = cache_name;
  info.event = "REQ";

  if (std::holds_alternative<WriteRequest>(req))
  {
    const auto& w = std::get<WriteRequest>(req);
    info.operation = "WR";
    info.address   = w.address;
    info.data      = w.data;
  }
  else
  {
    const auto& r = std::get<ReadRequest>(req);
    info.operation = "RD";
    info.address   = r.address;
    info.size      = r.size;
  }

  emit(info);
}

// Логирование ответа
void TraceLogger::response(std::string_view from, std::string_view to, bool is_write,
                           uint64_t address, const std::vector<std::byte>& data)
{
  const std::string cache_name = std::string(from) + "->" + std::string(to);

  TraceInfo info;
  info.name     = cache_name;
  info.event     = "RESP";
  info.operation = is_write ? "WR" : "RD";
  info.address   = address;
  info.data      = data;   // и для WR, и для RD — одинаково

  emit(info);
}

// Логирование ответа
void TraceLogger::response(std::string_view from, std::string_view to, Response resp,
                           uint64_t address)
{
  const std::string cache_name = std::string(from) + "->" + std::string(to);

  TraceInfo info;
  info.name     = cache_name;
  info.event     = "RESP";

  if (std::holds_alternative<WriteResponse>(resp))
  {
    info.operation = "WR";
    info.address   = address;
    info.other     = "success";
  }
  else
  {
    const auto& r = std::get<ReadResponse>(resp);
    info.operation = "RD";
    info.address   = address;
    info.data      = r.data;
  }

  emit(info);
}


// Логирование попадания в кэш
void TraceLogger::hit(std::string_view cache, bool is_write, uint64_t address,
                      size_t set, uint64_t tag, int way,
                      const std::map<std::string, std::string>& metadata,
                      const std::vector<std::byte>& data)
{
  TraceInfo info;
  info.name     = cache;
  info.event     = "HIT";
  info.operation = is_write ? "WR" : "RD";
  info.address   = address;
  info.set       = set;
  info.tag       = tag;
  info.way       = way;
  info.metadata  = metadata;
  info.data = data;

  emit(info);
}

// Логирование промаха в кэше
void TraceLogger::miss(std::string_view cache, bool is_write, uint64_t address,
                       size_t set, uint64_t tag)
{
  TraceInfo info;
  info.name     = cache;
  info.event     = "MISS";
  info.operation = is_write ? "WR" : "RD";
  info.address   = address;
  info.set       = set;
  info.tag       = tag;

  emit(info);
}

// Логирвание найденной жертвы замещения
void TraceLogger::evict(std::string_view cache, bool is_write, uint64_t address,
                        size_t set, uint64_t tag, int way,
                        const std::map<std::string, std::string>& metadata,
                        const std::vector<std::byte>& data)
{
  TraceInfo info;
  info.name     = cache;
  info.event     = "EVICT";
  info.operation = is_write ? "WR" : "RD";
  info.address   = address;
  info.set       = set;
  info.tag       = tag;
  info.way       = way;
  info.metadata  = metadata;
  info.data = data;

  emit(info);
}

// Логирование заполнения блока кэша
void TraceLogger::fill(std::string_view cache, bool is_write, uint64_t address,
                       size_t set, uint64_t tag, int way,
                       const std::map<std::string, std::string>& metadata,
                       const std::vector<std::byte>& data)
{
  TraceInfo info;
  info.name     = cache;
  info.event     = "FILL";
  info.operation = is_write ? "WR" : "RD";
  info.address   = address;
  info.set       = set;
  info.tag       = tag;
  info.way       = way;
  info.metadata  = metadata;
  info.data = data;

  emit(info);
}

// Логирование записи в кэш
void TraceLogger::write(std::string_view cache, uint64_t address,
             size_t set, uint64_t tag, int way, size_t offset,
             const std::map<std::string, std::string>& metadata,
             const std::vector<std::byte>& data)
{
  TraceInfo info;
  info.name     = cache;
  info.event     = "WRITE";
  info.operation = "WR";
  info.address   = address;
  info.set       = set;
  info.tag       = tag;
  info.way       = way;
  info.offset    = offset;
  info.metadata  = metadata;
  info.data = data;

  emit(info);
}

// Логирование чтения из кэша
void TraceLogger::read(std::string_view cache, uint64_t address,
                       size_t set, uint64_t tag, int way, size_t offset,
                       const std::map<std::string, std::string>& metadata,
                       const std::vector<std::byte>& data)
{
  TraceInfo info;
  info.name     = cache;
  info.event     = "READ";
  info.operation = "RD";
  info.address   = address;
  info.set       = set;
  info.tag       = tag;
  info.way       = way;
  info.offset    = offset;
  info.metadata  = metadata;
  info.data = data;

  emit(info);
}

void TraceLogger::update(std::string_view cache, bool is_write, uint64_t address,
                         size_t set, uint64_t tag, int way,
                         const std::map<std::string, std::string>& metadata,
                         const std::vector<std::byte>& data)
{
  TraceInfo info;
  info.name     = cache;
  info.event     = "UPDATE";
  info.operation = is_write ? "WR": "RD";
  info.address   = address;
  info.set       = set;
  info.tag       = tag;
  info.way       = way;
  info.metadata  = metadata;
  info.data = data;

  emit(info);
}
} // namespace cache::log

