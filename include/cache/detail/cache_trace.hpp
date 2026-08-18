#pragma once

/*
  Трассировка кэш-иерархии.
 
  cmake -DCACHE_ENABLE_LOG=ON|OFF
 
  В cache_controller.hpp:
    if constexpr (cache::log::kEnabled) { ... }
*/

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#ifndef CACHE_ENABLE_LOG
#define CACHE_ENABLE_LOG 0
#endif

namespace cache::log {

// Флаг для if constexpr
inline constexpr bool kEnabled = (CACHE_ENABLE_LOG != 0);

} // namespace cache::log
#define CACHE_ENABLE_LOG 1
#if CACHE_ENABLE_LOG

#include <sstream>
#include <iomanip>
#include <unordered_set>
#include <algorithm>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>

#include "policies.hpp"

namespace cache::log {

namespace logging  = boost::log;
namespace keywords = boost::log::keywords;

struct TraceCtx {
  std::string_view cache;
  std::string_view what;
  std::string_view from;
  std::string_view to;

  bool     is_write = false;
  uint64_t addr     = 0;
  size_t   size     = 0;

  size_t   set = 0;
  int      way = -1;
  uint64_t tag = 0;
  size_t   offset = 0;
  bool     has_offset = false;

  const char* state_from = nullptr;
  const char* state_to   = nullptr;

  uint64_t victim_tag  = 0;
  uint64_t victim_addr = 0;
  bool     victim_dirty = false;

  bool success = true;
  const std::vector<std::byte>* data = nullptr;
};

inline const char* state_name(WriteBackPolicy::State s) {
  switch (s) {
    case WriteBackPolicy::State::INVALID: return "INVALID";
    case WriteBackPolicy::State::CLEAN:   return "CLEAN";
    case WriteBackPolicy::State::DIRTY:   return "DIRTY";
  }
  return "?";
}

inline const char* state_name(WriteThroughPolicy::State s) {
  switch (s) {
    case WriteThroughPolicy::State::INVALID: return "INVALID";
    case WriteThroughPolicy::State::VALID:   return "VALID";
  }
  return "?";
}

template <typename WritePolicy>
struct StateName;

template <>
struct StateName<WriteBackPolicy> {
  static const char* get(const typename WriteBackPolicy::BlockMeta& m) {
    return state_name(m.state);
  }
};

template <>
struct StateName<WriteThroughPolicy> {
  static const char* get(const typename WriteThroughPolicy::BlockMeta& m) {
    return state_name(m.state);
  }
};

template <typename Block>
inline int way_of(const Block& block, const std::vector<Block>& set) {
  return static_cast<int>(&block - set.data());
}

inline std::vector<std::byte>
slice_data(const std::vector<std::byte>& block, size_t offset, size_t size) {
  if (offset + size > block.size()) return {};
  return {block.begin() + static_cast<std::ptrdiff_t>(offset),
          block.begin() + static_cast<std::ptrdiff_t>(offset + size)};
}

inline std::unordered_set<std::string>& allowed_caches() {
  static std::unordered_set<std::string> s;
  return s;
}

inline bool pass(std::string_view name) {
  auto& a = allowed_caches();
  if (a.empty() || a.count("*")) return true;
  return a.count(std::string(name)) != 0;
}

inline bool pass_ctx(const TraceCtx& c) {
  if (pass(c.cache)) return true;
  if (!c.from.empty() && pass(c.from)) return true;
  if (!c.to.empty()   && pass(c.to))   return true;
  return false;
}

inline void init(std::string_view filter = "*") {
  allowed_caches().clear();
  if (filter.empty() || filter == "*") {
    allowed_caches().insert("*");
  } else {
    std::string fc(filter);
    for (size_t i = 0; i < fc.size();) {
      while (i < fc.size() && (fc[i] == ' ' || fc[i] == ',')) ++i;
      size_t j = i;
      while (j < fc.size() && fc[j] != ',') ++j;
      if (j > i) allowed_caches().insert(fc.substr(i, j - i));
      i = j + 1;
    }
  }
  logging::add_common_attributes();
  logging::add_console_log(
      std::clog,
      keywords::format = "%Message%",
      keywords::auto_flush = true);
}

inline void append_data(std::ostringstream& o, const std::vector<std::byte>* data) {
  if (!data || data->empty()) return;
  
  const auto& d = *data;
  const size_t total = d.size();
  
  o << " data=";
  
  // Проверяем, всe ли нули
  bool all_zero = std::all_of(d.begin(), d.end(), 
      [](std::byte b) { return b == std::byte{0}; });
  
  if (all_zero) {
    o << "0x" << std::hex << total << std::dec ;
    return;
  }
  
  // Находим границы ненулевых данных
  size_t first_nonzero = 0;
  while (first_nonzero < total && d[first_nonzero] == std::byte{0}) {
    ++first_nonzero;
  }
  
  size_t last_nonzero = total - 1;
  while (last_nonzero > first_nonzero && d[last_nonzero] == std::byte{0}) {
    --last_nonzero;
  }
  
  // Если ненулевой кусок короткий — показываем с нулями по краям
  size_t nonzero_len = last_nonzero - first_nonzero + 1;
  
  if (first_nonzero > 0) {
    o << "[" << first_nonzero << "×00] ";
  }
  
  // Показываем ненулевые байты
  size_t show_bytes = std::min(nonzero_len, size_t(16));
  for (size_t i = first_nonzero; i < first_nonzero + show_bytes; ++i) {
    if (i > first_nonzero) o << " ";
    o << std::hex << std::setw(2) << std::setfill('0')
      << static_cast<unsigned>(d[i]);
  }
  o << std::dec;
  
  if (nonzero_len > 16) {
    o << " ...";
  }
  
  if (last_nonzero < total - 1) {
    o << " [" << (total - 1 - last_nonzero) << "×00]";
  }
}

inline void emit(const TraceCtx& c) {
  if (!pass_ctx(c)) return;

  std::ostringstream o;

  if (c.what == "REQ_DOWN" || c.what == "RESP_UP") {
    std::string_view label = (c.what == "REQ_DOWN") ? "REQ" : "RESP";
    o << '[' << (c.from.empty() ? c.cache : c.from)
      << "→" << (c.to.empty() ? "?" : c.to) << "] "
      << label << ' '
      << (c.is_write ? "WR" : "RD")
      << " addr=0x" << std::hex << c.addr << std::dec;
    if (c.size) o << " sz=" << c.size;
    if (c.what == "REQ_DOWN" && c.is_write) append_data(o, c.data);
    if (c.what == "RESP_UP"  && !c.is_write) append_data(o, c.data);
    if (c.what == "RESP_UP"  && !c.success) o << " FAIL";
  } else {
    o << '[' << c.cache << "] " << c.what;

    if (c.what == "BYPASS" || c.what == "WT") {
      o << ' ' << (c.is_write ? "WR" : "RD")
        << " addr=0x" << std::hex << c.addr << std::dec;
      if (c.size) o << " sz=" << c.size;
      if (c.is_write) append_data(o, c.data);
    }

    if (c.what == "HIT" || c.what == "ALLOC" ||
        c.what == "FILL" || c.what == "STATE" || c.what == "EVICT") {
      o << " set=" << c.set << " way=" << c.way
        << " tag=0x" << std::hex << c.tag << std::dec;
    }


    if (c.has_offset && (c.what == "HIT" || c.what == "FILL" || c.what == "EVICT"))
      o << " offset=" << c.offset;

    if (c.what == "STATE" && c.state_from && c.state_to)
      o << ' ' << c.state_from << "→" << c.state_to;

    if (c.what == "EVICT") {
      o << " victim_tag=0x" << std::hex << c.victim_tag
        << " victim_addr=0x" << c.victim_addr << std::dec
        << (c.victim_dirty ? " DIRTY" : " clean");
    }

    if (c.what == "WB") {
      o << " addr=0x" << std::hex << c.addr << std::dec;
      append_data(o, c.data);
    }

    if (c.what == "HIT" || c.what == "FILL" || c.what == "EVICT")
      append_data(o, c.data);
  }

  BOOST_LOG_TRIVIAL(info) << o.str();
}

class Tracer {
  std::string_view m_cache;
  std::string_view m_lower;

  TraceCtx base(std::string_view what) const {
    TraceCtx c;
    c.cache = m_cache;
    c.what  = what;
    return c;
  }

public:
  Tracer(std::string_view cache, std::string_view lower)
    : m_cache(cache), m_lower(lower) {}

  void hit(size_t set, int way, uint64_t tag, size_t offset,
           bool is_write, uint64_t addr, size_t size,
           const std::vector<std::byte>* data) const {
    auto c = base("HIT");
    c.set = set; c.way = way; c.tag = tag;
    c.offset = offset; c.has_offset = true;
    c.is_write = is_write; c.addr = addr; c.size = size; c.data = data;
    emit(c);
  }

  void miss(size_t set, uint64_t tag, bool is_write, uint64_t addr) const {
    auto c = base("MISS");
    c.set = set; c.way = -1; c.tag = tag;
    c.is_write = is_write; c.addr = addr;
    emit(c);
  }

  void alloc(size_t set, int way, uint64_t tag) const {
    auto c = base("ALLOC");
    c.set = set; c.way = way; c.tag = tag;
    emit(c);
  }

  void fill(size_t set, int way, uint64_t tag,
            const std::vector<std::byte>* data) const {
    auto c = base("FILL");
    c.set = set; c.way = way; c.tag = tag;
    c.offset = 0; c.has_offset = true; c.data = data;
    emit(c);
  }

  void state(size_t set, int way, uint64_t tag,
             const char* from, const char* to) const {
    if (std::string_view(from) == std::string_view(to)) return;
    auto c = base("STATE");
    c.set = set; c.way = way; c.tag = tag;
    c.state_from = from; c.state_to = to;
    emit(c);
  }

  void evict(size_t set, int way, uint64_t victim_tag, uint64_t victim_addr,
             bool dirty, const std::vector<std::byte>* data) const {
    auto c = base("EVICT");
    c.set = set; c.way = way; c.tag = victim_tag;
    c.offset = 0; c.has_offset = true;
    c.victim_tag = victim_tag; c.victim_addr = victim_addr;
    c.victim_dirty = dirty; c.data = data;
    emit(c);
  }

  void wb(uint64_t addr, const std::vector<std::byte>* data) const {
    auto c = base("WB");
    c.addr = addr;
    if (data) c.size = data->size();
    c.data = data;
    emit(c);
  }

  void bypass(bool is_write, uint64_t addr, size_t size,
              const std::vector<std::byte>* data) const {
    auto c = base("BYPASS");
    c.is_write = is_write; c.addr = addr; c.size = size; c.data = data;
    emit(c);
  }

  void req_down(bool is_write, uint64_t addr, size_t size,
                const std::vector<std::byte>* data = nullptr) const {
    auto c = base("REQ_DOWN");
    c.from = m_cache; c.to = m_lower; c.what = "REQ_DOWN";
    c.is_write = is_write; c.addr = addr; c.size = size; c.data = data;
    emit(c);
  }

  void resp_up(bool is_write, uint64_t addr, size_t size, bool success,
               const std::vector<std::byte>* data = nullptr) const {
    auto c = base("RESP_UP");
    c.from = m_lower; c.to = m_cache; c.what = "RESP_UP";
    c.is_write = is_write; c.addr = addr; c.size = size;
    c.success = success; c.data = data;
    emit(c);
  }
};

} // namespace cache::log

#define CACHE_LOG_INIT(...) ::cache::log::init(__VA_ARGS__)
#define CACHE_LOG(ctx)      ::cache::log::emit(ctx)

#else  // CACHE_ENABLE_LOG == 0 — заглушки для if constexpr

namespace cache::log {

struct TraceCtx {
  std::string_view cache;
  std::string_view what;
  std::string_view from;
  std::string_view to;
  bool is_write = false;
  uint64_t addr = 0;
  size_t size = 0;
  size_t set = 0;
  int way = -1;
  uint64_t tag = 0;
  size_t offset = 0;
  bool has_offset = false;
  const char* state_from = nullptr;
  const char* state_to = nullptr;
  uint64_t victim_tag = 0;
  uint64_t victim_addr = 0;
  bool victim_dirty = false;
  bool success = true;
  const std::vector<std::byte>* data = nullptr;
};

inline void init(std::string_view = "*") {}
inline void emit(const TraceCtx&) {}

// Заглушки
// сама ветка при kEnabled==false не инстанцируется в шаблонах.
template <typename WritePolicy>
struct StateName {
  template <typename Meta>
  static const char* get(const Meta&) { return "?"; }
};

template <typename Block>
inline int way_of(const Block&, const std::vector<Block>&) { return -1; }

inline std::vector<std::byte>
slice_data(const std::vector<std::byte>&, size_t, size_t) { return {}; }

class Tracer {
public:
  Tracer(std::string_view, std::string_view) {}
  void hit(size_t, int, uint64_t, size_t, bool, uint64_t, size_t,
           const std::vector<std::byte>*) const {}
  void miss(size_t, uint64_t, bool, uint64_t) const {}
  void alloc(size_t, int, uint64_t) const {}
  void fill(size_t, int, uint64_t, const std::vector<std::byte>*) const {}
  void state(size_t, int, uint64_t, const char*, const char*) const {}
  void evict(size_t, int, uint64_t, uint64_t, bool,
             const std::vector<std::byte>*) const {}
  void wb(uint64_t, const std::vector<std::byte>*) const {}
  void bypass(bool, uint64_t, size_t, const std::vector<std::byte>*) const {}
  void req_down(bool, uint64_t, size_t,
                const std::vector<std::byte>* = nullptr) const {}
  void resp_up(bool, uint64_t, size_t, bool,
               const std::vector<std::byte>* = nullptr) const {}
};

} // namespace cache::log

#define CACHE_LOG_INIT(...) ((void)0)
#define CACHE_LOG(ctx)      ((void)0)

#endif
