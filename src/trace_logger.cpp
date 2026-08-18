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

  std::vector<uint32_t> words;
  words.reserve((data.size() + 3) / 4);
  for (size_t i = 0; i < data.size(); i += 4)
  {
    uint32_t word = 0;
    for (size_t j = 0; j < 4 && (i + j) < data.size(); ++j)
    {
      word |= (static_cast<uint32_t>(data[i + j]) << ((3 - j) * 8));
    }
    words.push_back(word);
  }

  if (std::all_of(words.begin(), words.end(), [](uint32_t w) { return w == 0; }))
  {
    return "0";
  }

  size_t first = 0;
  while (first < words.size() && words[first] == 0)
  {
    ++first;
  }
  size_t last = words.size();
  while (last > first && words[last - 1] == 0)
  {
    --last;
  }

  std::string result;
  for (size_t i = first; i < last; ++i)
  {
    if (i > first)
    {
      result += '\'';
    }
    result += std::format("{:08x}", words[i]);
  }
  return result;
}

// Форматирование метаданных политик ...
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
  msg << std::showbase << "addr=" << std::hex << info.address << std::dec << std::noshowbase;

  // size=  (только для RD в REQ)
  if (info.event == "REQ" && info.operation == "RD" && info.size != 0)
  {
    msg << " size=" << info.size;
  }

  // set= tag= way= offset=
  if (info.set)
  {
    msg << " set=" << info.set;
  }
  if (info.tag)
  {
    msg << std::showbase << " tag=" << std::hex << info.tag << std::dec << std::noshowbase;
  }
  if (info.way)
  {
    msg << " way=" << info.way;
  }
  if (info.offset)
  {
    msg << " offset=" << info.offset;
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

