#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <variant>
#include <any>
#include <typeindex>
#include <stdexcept>
#include <unordered_map>
#include <map>
#include <string>
#include <string_view>
#include <iomanip>

#include "interfaces.hpp"
#include "cache/detail/policies.hpp"
#include "cache/detail/cache_controller.hpp"
#include "cache/detail/trace_logger.hpp"

namespace cache {

// Общий интерфейс «нижнего уровня» (кэш или память)
struct ILowerLevel
{
  virtual ~ILowerLevel() = default;
  
  // Принять запрос сверху и вернуть ответ
  virtual Response handle(const Request& req) = 0;
  virtual std::string_view name() const = 0;
};

// Модель простой памяти ()
class SimpleMemory : public ILowerLevel
{
  size_t m_block_size;
  // Хранение данных (адресс, блок данных)
  std::unordered_map<uint64_t, std::vector<std::byte>> m_storage;
  // Имя для объекта памяти
  std::string m_name{"MEM"};

public:
  explicit SimpleMemory(size_t block_size = 64) : m_block_size(block_size)
  {}

  // Определение имени
  void set_name(std::string name)
  {
    m_name = std::move(name);
  }

  // Получение имени
  std::string_view name() const override
  {
    return m_name;
  }

  // Размер блока
  size_t block_size() const noexcept
  {
    return m_block_size;
  }

  // Обработка запроса в память
  Response handle(const Request& req) override
  {
    // Запись
    if (std::holds_alternative<WriteRequest>(req))
    {
      const auto& w = std::get<WriteRequest>(req);
      
      size_t data_size = w.data.size();
      size_t offset_in_data = 0;
      uint64_t current_addr = w.address;
      
      // Пишем данные пока они не закончатся
      while (offset_in_data < data_size)
      {
        // Выравниваем адрес до границы блока
        uint64_t block_addr = (current_addr / m_block_size) * m_block_size;
        
        // Вычисляем смещение внутри блока
        size_t offset = current_addr - block_addr;
        
        // Сколько байт можно записать в этот блок
        size_t bytes_to_write = std::min(data_size - offset_in_data, m_block_size - offset);
        
        // Если данных по этому адресу еще нет - создаем блок из нулей
        auto it = m_storage.find(block_addr);
        if (it == m_storage.end())
        {
          m_storage[block_addr] = std::vector<std::byte>(m_block_size, std::byte{0});
          it = m_storage.find(block_addr);
        }
        
        // Получаем ссылку на блок
        auto& block = it->second;
        
        // Копируем данные в блок
        for (size_t i = 0; i < bytes_to_write; ++i)
        {
          block[offset + i] = w.data[offset_in_data + i];
        }
        
        // Переходим к следующей части данных
        offset_in_data += bytes_to_write;
        current_addr += bytes_to_write;
      }
      return WriteResponse{true};
    }
    // Чтение
    else
    {
      const auto& r = std::get<ReadRequest>(req);
      
      // Выравниваем адрес до границы блока
      uint64_t block_addr = (r.address / m_block_size) * m_block_size;
      auto it = m_storage.find(block_addr);

      // Если данные по адресу найдены
      if (it != m_storage.end())
      {
        std::vector<std::byte> out = it->second;
        
        // Корректируем размер вывода, если он не совпадает с запросом
        if (out.size() < r.size)
        {
          out.resize(r.size, std::byte{0});
        }
        else if (out.size() > r.size)
        {
          out.resize(r.size);
        }
        return ReadResponse{std::move(out), true};
      }
      // Если промах в памяти, добавляем и возвращаем нули
      else
      {
        // Записываем блок из нулей в память
        m_storage[block_addr] = std::vector<std::byte>(m_block_size, std::byte{0});
        return ReadResponse{std::vector<std::byte>(r.size, std::byte{0}), true};
      }
    }
  }

  // Загрузка начальных данных (для тестов)
  void seed(uint64_t address, std::vector<std::byte> data)
  {
    m_storage[address] = std::move(data);
  }

  // Снимок: ключ = block address
  void dump(std::ostream& out) const
  {
    // стабильный порядок по адресу
    std::map<uint64_t, const std::vector<std::byte>*> ordered;
    for (const auto& [addr, data] : m_storage)
    {
      ordered.emplace(addr, &data);
    }
    for (const auto& [key, pdata] : ordered)
    {
      out << std::hex << "0x" << key << std::dec << " data=";
      // Правильный вывод данных
      {
        const auto& data = *pdata;
        out << std::hex << std::setfill('0');
        size_t size = data.size();
        
        for (size_t i = 0; i < m_block_size; i += 4)
        {
          if (i != 0) {
            out << '\'';
          }

          // Запоминаем позицию в потоке перед началом записи текущего 4-байтового слова
          for (size_t j = 0; j < 4; ++j)
          {
            size_t index = m_block_size - 1 - i - j;
            // Используем промежуточную строку
            unsigned int val;
            
            // Если данные заканчиваются, то заполняем нулями
            if (index < size)
            {
              val = std::to_integer<unsigned int>(data[index]);
            }
            else
            {
              val = 0;
            }
            
            out << std::setw(2) << val;
          }
        } 
        out << std::dec;
      }
      out << '\n';
    }
  }

};

// Один уровень кэша
// Внутри шаблонный CacheController с выбранными политиками
class Cache : public ILowerLevel
{
public:
  // Создание с конкретными политиками
  template <typename Repl, typename Write, typename Alloc>
  static Cache make(size_t num_sets, size_t associativity, size_t block_size)
  {
    Cache c;
    c.m_impl = std::make_unique<TypedImpl<Repl, Write, Alloc>>(
        num_sets, associativity, block_size);
    return c;
  }

  Cache(Cache&&) noexcept = default;
  Cache& operator=(Cache&&) noexcept = default;
  Cache(const Cache&) = delete;
  Cache& operator=(const Cache&) = delete;

  // Связка иерархии

  // Подключить нижний уровень (другой Cache или Memory).
  void set_lower(ILowerLevel& lower)
  {
    // указатель на нижний уровень
    m_lower = &lower;
    if (m_impl)
    {
      m_impl->set_lower_name(std::string(lower.name()));
    }
  }

  // Удалить нижний уровень
  void clear_lower()
  {
    m_lower = nullptr;
    if (m_impl)
    {
      m_impl->set_lower_name("?");
    }
  }

  // Добавить имя кэшу
  void set_name(std::string name)
  {
    if (m_impl)
    {
      m_impl->set_name(std::move(name));
    }
  }

  std::string_view name() const override
  {
    return m_impl ? m_impl->name() : "?";
  }

  // Получить нижний уровень
  ILowerLevel* lower() const noexcept
  {
    return m_lower;
  }


  // Основной API

  // Запрос с верхнего уровня 
  Response process(const Request& req)
  {

    Response resp = handle(req);
    

    return resp;
  }

  Response handle(const Request& req) override
  {
    // если нет подключенного нижнего уровня
    if (!m_impl)
    {
      throw std::runtime_error("Cache: not initialized");
    }

    // Определение фукнции запроса к нижнему уровню
    std::function<void(const Request&)> to_lower = [this](const Request& r)
    {
      m_pending = r;
    };

    // Определение функции ответа от нижнего уровня
    std::function<Response()> from_lower = [this]() -> Response
    {
      // Если есть запрос
      if (!m_pending)
      {
        return ReadResponse{};
      }
      // удаляем запрос
      Request r = *m_pending;
      m_pending.reset();
      if (!m_lower)
      {
        throw std::runtime_error("LowerCache: not initialized");
      }
      return m_lower->handle(r);
    };

    return m_impl->process(req, to_lower, from_lower);
  }



  // Размер блока
  size_t block_size() const
  {
    return m_impl ? m_impl->block_size() : 0;
  }
  
  // Размер набора
  size_t num_sets() const
  {
    return m_impl ? m_impl->num_sets() : 0;
  }

  // Ассоциативность
  size_t associativity() const
  {
    return m_impl ? m_impl->associativity() : 0;
  }

    // Снимок состояния валидных блоков (ключ = block address)
  void dump(std::ostream& out) const
  {
    if (m_impl)
    {
      m_impl->dump(out);
    }
  }


private:
  Cache() = default;

  // Общий интерфейс запроса от верхнего уровня
  struct IImpl
  {
    virtual ~IImpl() = default;
    virtual Response process(const Request& req,
                             std::function<void(const Request&)>& to_lower,
                             std::function<Response()>& from_lower) = 0;

    virtual size_t block_size() const = 0;

    virtual size_t num_sets() const = 0;

    virtual size_t associativity() const = 0;

    virtual void set_name(std::string name) = 0;

    virtual void set_lower_name(std::string name) = 0;

    virtual std::string_view name() const = 0;

    virtual void dump(std::ostream& out) const = 0;
  };

  // Просто оболочка над контроллером чтобы определять их одного типа
  template <typename Repl, typename Write, typename Alloc>
  class TypedImpl final : public IImpl
  {
    detail::CacheController<Repl, Write, Alloc> m_controller;
  public:
    TypedImpl(size_t sets, size_t assoc, size_t blk)
      : m_controller(sets, assoc, blk) {}

    // Выполнение запроса
    Response process(const Request& req,
                     std::function<void(const Request&)>& to_lower,
                     std::function<Response()>& from_lower) override
    {
      return m_controller.process_transaction(req, to_lower, from_lower);
    }

    // Определение имени
    void set_name(std::string name) override
    {
      m_controller.set_name(std::move(name));
    }

    // Определение нижнего кэша по иерархии
    void set_lower_name(std::string name) override
    {
      m_controller.set_lower_name(std::move(name));
    }

    // Получить имя
    std::string_view name() const override
    {
      return m_controller.name();
    }

    // Размер блока
    size_t block_size() const override
    {
      return m_controller.get_block_size();
    }

    // Размер набора
    size_t num_sets() const override
    {
      return m_controller.get_num_sets();
    }

    // Ассоциативность
    size_t associativity() const override
    {
      return m_controller.get_associativity();
    }

    // выгрузка данных
    void dump(std::ostream& out) const override
    {
      m_controller.dump(out);
    }
  };

  // Сам кэш
  std::unique_ptr<IImpl> m_impl;
  /// Указатель на нижний по иерархии уровень
  ILowerLevel* m_lower = nullptr;
  // Буффер для запроса хранения запроса который отправляет вниз
  std::optional<Request> m_pending;
};

// Фабрика с политиками по умолчанию: LRU + Write-Back + Write-Read-Allocate
template <typename Repl = LRUPolicy,
          typename Write = WriteBackPolicy,
          typename Alloc = AllAllocatePolicy>
Cache make_cache(std::string name, size_t num_sets, size_t associativity, size_t block_size)
{
  auto c = Cache::make<Repl, Write, Alloc>(num_sets, associativity, block_size);
  c.set_name(std::move(name));
  return c;
}

inline SimpleMemory make_memory(std::string name, size_t block_size = 64)
{
  SimpleMemory m(block_size);
  m.set_name(std::move(name));
  return m;
}

// Иерархия кэшей
class Hierarchy
{
public:
  Hierarchy() = default;
  Hierarchy(Hierarchy&&) noexcept = default;
  Hierarchy& operator=(Hierarchy&&) noexcept = default;
  Hierarchy(const Hierarchy&) = delete;
  Hierarchy& operator=(const Hierarchy&) = delete;

  Hierarchy& add(Cache cache)
  {
    m_levels.push_back(std::move(cache));
    m_linked = false;
    return *this;
  }

  Hierarchy& memory(SimpleMemory mem)
  {
    m_mem = std::move(mem);
    m_linked = false;
    return *this;
  }

  // Задать имя уровня, от которого идут запросы 
  Hierarchy& set_requester(std::string name)
  {
    m_requester = std::move(name);
    return *this;
  }

  // Название запрашивающего объекта
  const std::string& requester() const
  {
    return m_requester;
  }

  Cache& top()
  {
    ensure_linked();
    return m_levels.front();
  }

  // Ссылка на объект нижнего уровня
  const Cache& top() const
  {
    return m_levels.front();
  }

  Response process(const Request& req)
  {
    ensure_linked();
    const auto& top_name = m_levels.front().name();

    // Переворачиваем байты при запросе на запись
    Request internal = req;
    if (std::holds_alternative<WriteRequest>(internal))
    {
      std::reverse(std::get<WriteRequest>(internal).data.begin(),
                   std::get<WriteRequest>(internal).data.end());
    }

    auto& log = cache::log::TraceLogger::instance();
    log.request(m_requester, top_name, internal);
    Response resp = top().process(internal);

    // Переворачиваем байты при ответе на чтение
    if (std::holds_alternative<ReadResponse>(resp))
    {
      std::reverse(std::get<ReadResponse>(resp).data.begin(),
                   std::get<ReadResponse>(resp).data.end());
    }
    
    log.response(top_name, m_requester, internal, resp);
    return resp;
  }

  SimpleMemory& mem()
  {
    if (!m_mem)
    {
      throw std::runtime_error("Hierarchy: no memory attached");
    }
    return *m_mem;
  }

  size_t levels() const noexcept
  {
    return m_levels.size();
  }
  //
  // Выдача состояния всех кэшей и памяти в каталог
  // Файлы: <dir>/<NAME>.state, записи по ключу (block address)
  void dump_state(std::string_view dir) const
  {
    std::filesystem::create_directories(dir);
    for (const auto& level : m_levels)
    {
      const std::string path =
          std::string(dir) + "/" + std::string(level.name()) + ".state";
      std::ofstream f(path);
      if (!f)
      {
        throw std::runtime_error("dump_state: cannot open " + path);
      }
      f << "# key=block_address  level=" << level.name() << "\n";
      level.dump(f);
    }
    if (m_mem)
    {
      const std::string path =
          std::string(dir) + "/" + std::string(m_mem->name()) + ".state";
      std::ofstream f(path);
      if (!f)
      {
        throw std::runtime_error("dump_state: cannot open " + path);
      }
      f << "# key=block_address  level=" << m_mem->name() << "\n";
      m_mem->dump(f);
    }
  }

private:
  std::vector<Cache> m_levels;
  std::optional<SimpleMemory> m_mem;
  std::string m_requester{"CPU"};
  bool m_linked = false;

  void ensure_linked()
  {
    if (m_linked)
    {
      return;
    }

    if (m_levels.empty())
    {
      throw std::runtime_error("Hierarchy: no cache levels");
    }

    for (size_t i = 0; i + 1 < m_levels.size(); ++i)
    {
      m_levels[i].set_lower(m_levels[i + 1]);
    }

    if (m_mem)
    {
      m_levels.back().set_lower(*m_mem);
    }
    m_linked = true;
  }
};

namespace detail {
inline void hierarchy_append(Hierarchy& h, Cache& c)
{
  h.add(std::move(c));
}

inline void hierarchy_append(Hierarchy& h, Cache&& c)
{
  h.add(std::move(c));
}

inline void hierarchy_append(Hierarchy& h, SimpleMemory& m)
{
  h.memory(std::move(m));
}

inline void hierarchy_append(Hierarchy& h, SimpleMemory&& m)
{
  h.memory(std::move(m));
}
}

// Порядок аргументов = сверху вниз: L1, L2, …, MEM
template <typename... Levels>
Hierarchy make_hierarchy(Levels&&... levels)
{
  Hierarchy h;
  (detail::hierarchy_append(h, std::forward<Levels>(levels)), ...);
  return h;
}

// Связь несокльких кэшей и памяти
// levels[0] → levels[1] → ... → levels.back() → memory
// Все объекты должны оставаться живыми.
inline void link_hierarchy(std::vector<Cache*> levels, ILowerLevel* memory = nullptr)
{
  if (levels.empty())
  {
    return;
  }
  for (size_t i = 0; i + 1 < levels.size(); ++i)
  {
    if (!levels[i] || !levels[i + 1])
    {
      throw std::invalid_argument("link_hierarchy: null Cache pointer");
    }
    levels[i]->set_lower(*levels[i + 1]);
  }
  if (memory)
  {
    levels.back()->set_lower(*memory);
  }
}

} // namespace cache
