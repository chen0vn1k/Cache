#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <variant>
#include <any>
#include <typeindex>
#include <stdexcept>
#include <unordered_map>

#include "interfaces.hpp"
#include "cache/detail/policies.hpp"
#include "cache/detail/cache_controller.hpp"

namespace cache {

// Общий интерфейс «нижнего уровня» (кэш или память)
struct ILowerLevel {
  virtual ~ILowerLevel() = default;
  
  // Принять запрос сверху и вернуть ответ
  virtual Response handle(const Request& req) = 0;
};

// Модель простой памяти ()
class SimpleMemory : public ILowerLevel {
  size_t m_block_size;
  // Хранение данных (адресс, блок данных)
  std::unordered_map<uint64_t, std::vector<std::byte>> m_storage;
  // Имя для объекта памяти
  std::string m_name{"MEM"};

public:
  explicit SimpleMemory(size_t block_size = 64) : m_block_size(block_size) {}

  // Определение имени (для логов)
  void set_name(std::string name) { m_name = std::move(name); }

  size_t block_size() const noexcept { return m_block_size; }

  // Обработка запроса в память
  Response handle(const Request& req) override {
    // Запись
    if (std::holds_alternative<WriteRequest>(req)) {
      const auto& w = std::get<WriteRequest>(req);
      
      // Записываем блок в память 
      m_storage[w.address] = w.data; 
      
      return WriteResponse{true};
    }
    // Чтение
    else {
      const auto& r = std::get<ReadRequest>(req);
      auto it = m_storage.find(r.address);

      // Если данные по адресу найдены
      if (it != m_storage.end()) {
        std::vector<std::byte> out = it->second;
        
        // Корректируем размер вывода, если он не совпадает с запросом
        if (out.size() < r.size) { out.resize(r.size, std::byte{0}); }
        else if (out.size() > r.size) { out.resize(r.size); }
        
        return ReadResponse{std::move(out), true};
    }

    // Если промах в памяти, возвращаем нули
    return ReadResponse{std::vector<std::byte>(r.size, std::byte{0}), true};
    }
  }

  // Загрузка начальных данных (для тестов)
  void seed(uint64_t address, std::vector<std::byte> data) {
    m_storage[address] = std::move(data);
  }
};

// Один уровень кэша
// Тип Cache (можно класть в контейнеры, передавать по ссылке)
// Внутри шаблонный CacheController с выбранными политиками
class Cache : public ILowerLevel {
public:
  // Создание с конкретными политиками
  template <typename Repl, typename Write, typename Alloc>
  static Cache make(size_t num_sets, size_t associativity, size_t block_size) {
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
  void set_lower(ILowerLevel& lower) {
    // указатель на нижний уровень
    m_lower = &lower;
  }

  // Удалить нижний уровень
  void clear_lower() {
    m_lower = nullptr;
  }

  // Добавить имя кэшу
  void set_name(std::string name) {
    if (m_impl) { m_impl->set_name(std::move(name)); }
  }

  // Получить нижний уровень
  ILowerLevel* lower() const noexcept { return m_lower; }


  // Основной API

  // Запрос с верхнего уровня (кэш или процессор)
  Response process(const Request& req) {
    return handle(req);
  }

  Response handle(const Request& req) override {
    // если нет подключенного нижнего уровня
    if (!m_impl)
      throw std::runtime_error("Cache: not initialized");

    // Определение фукнции запроса к нижнему уровню
    std::function<void(const Request&)> to_lower = [this](const Request& r) {
      m_pending = r;
    };
    // Определение функции ответа от нижнего уровня
    std::function<Response()> from_lower = [this]() -> Response {
      // Если есть запрос
      if (!m_pending)
        return ReadResponse{};
      // удаляем запрос
      Request r = *m_pending;
      m_pending.reset();
      if (!m_lower) {
        throw std::runtime_error("LowerCache: not initialized"); 
      }
      return m_lower->handle(r);
    };

    return m_impl->process(req, to_lower, from_lower);
  }



  size_t block_size() const { return m_impl ? m_impl->block_size() : 0; }
  size_t num_sets() const { return m_impl ? m_impl->num_sets() : 0; }
  size_t associativity() const { return m_impl ? m_impl->associativity() : 0; }

private:
  Cache() = default;

  // Общий интерфейс запроса от верхнего уровня
  struct IImpl {
    virtual ~IImpl() = default;
    virtual Response process(const Request& req,
                             std::function<void(const Request&)>& to_lower,
                             std::function<Response()>& from_lower) = 0;
    virtual size_t block_size() const = 0;
    virtual size_t num_sets() const = 0;
    virtual size_t associativity() const = 0;

    virtual void set_name(std::string name) = 0;
  };

  // Просто оболочка над контроллером чтобы определять их одного типа
  template <typename Repl, typename Write, typename Alloc>
  class TypedImpl final : public IImpl {
    detail::CacheController<Repl, Write, Alloc> m_controller;
  public:
    TypedImpl(size_t sets, size_t assoc, size_t blk)
      : m_controller(sets, assoc, blk) {}

    Response process(const Request& req,
                     std::function<void(const Request&)>& to_lower,
                     std::function<Response()>& from_lower) override {
      return m_controller.process_transaction(req, to_lower, from_lower);
    }



    void set_name(std::string name) override {
      m_controller.set_name(std::move(name));
    }

    size_t block_size() const override { return m_controller.get_block_size(); }
    size_t num_sets() const override { return m_controller.get_num_sets(); }
    size_t associativity() const override { return m_controller.get_associativity(); }
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
Cache make_cache(size_t num_sets, size_t associativity, size_t block_size) {
  return Cache::make<Repl, Write, Alloc>(num_sets, associativity, block_size);
}

// Связь несокльких кэшей и памяти
// levels[0] → levels[1] → ... → levels.back() → memory
// Все объекты должны оставаться живыми.
inline void link_hierarchy(std::vector<Cache*> levels, ILowerLevel* memory = nullptr) {
  if (levels.empty()) return;
  for (size_t i = 0; i + 1 < levels.size(); ++i) {
    if (!levels[i] || !levels[i + 1])
      throw std::invalid_argument("link_hierarchy: null Cache pointer");
    levels[i]->set_lower(*levels[i + 1]);
  }
  if (memory)
    levels.back()->set_lower(*memory);
}

} // namespace cache

