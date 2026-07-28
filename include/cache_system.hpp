#pragma once

#include <optional>
#include <functional>
#include <iostream>
#include <vector>
#include <variant>

#include "interfaces.hpp"
#include "cache/detail/policies.hpp"
#include "cache/detail/cache_controller.hpp"

namespace cache {

// Воспомогательные фукнции
namespace detail {

// простая модель памяти
struct SimpleMemory {
  size_t block_size;
  explicit SimpleMemory(size_t bs) : block_size(bs) {}

  void operator()(const Request& req) {
    if (std::holds_alternative<ReadRequest>(req)) {
      const auto& r = std::get<ReadRequest>(req);
      std::clog << "    [Mem] READ  addr=0x" << std::hex << r.address
                << std::dec << " size=" << r.size << "\n";
    } else {
      const auto& w = std::get<WriteRequest>(req);
      std::clog << "    [Mem] WRITE addr=0x" << std::hex << w.address
                << std::dec << " size=" << w.data.size() << "\n";
    }
  }

  Response get_response() {
    std::clog << "    [Mem] Returning zero-filled block (" << block_size << " bytes)\n";
    return ReadResponse{std::vector<std::byte>(block_size, std::byte{0})};
  }
};

// Коннектор между кэшами
template <typename Controller>
class CacheAsLowerLevel {
public:
  // Нижний кэш
  Controller& ctrl;
  // Обращения к памяти от нижнего кэша
  std::function<void(const Request&)> to_lower;
  std::function<Response()> from_lower;
  // Запрос который будет храниться пока его не попросят
  std::optional<Request> pending_req;

  
  CacheAsLowerLevel(Controller& c,
                    std::function<void(const Request&)> to_l,
                    std::function<Response()> from_l)
    : ctrl(c), to_lower(std::move(to_l)), from_lower(std::move(from_l))
  {}

  void operator()(const Request& req) {
    // Запоминаем запрос
    pending_req = req;
    // запись обрабатываем сразу
    if (std::holds_alternative<WriteRequest>(req)) {
      auto dummy_res = [&]() -> Response { return from_lower(); };
      ctrl.process_transaction(req, to_lower, dummy_res);
    }
  }

  // Ответ, когда его просит верхний кэш
  Response get_response() {
    if (!pending_req) return ReadResponse{};
    auto req = *pending_req;
    // обнуляем запрос после ответа
    pending_req.reset();
    return ctrl.process_transaction(req, to_lower, from_lower);
  }
};

} // namespace detail

/**
 * Двухуровневая кэш-система.
 *
 * По умолчанию:
 *   L1 — LRU + Write-Back + Write-Allocate
 *   L2 — FIFO + Write-Through + Write-Allocate
 *
 */
template <
  typename L1Replacement = LRUPolicy,
  typename L1Write = WriteBackPolicy,
  typename L1Allocation = WriteAllocatePolicy,
  typename L2Replacement = FIFOPolicy,
  typename L2Write = WriteThroughPolicy,
  typename L2Allocation = WriteAllocatePolicy
>
class CacheSystem {
public:
  using L1Controller = detail::CacheController<L1Replacement, L1Write, L1Allocation>;
  using L2Controller = detail::CacheController<L2Replacement, L2Write, L2Allocation>;

private:
  L1Controller m_l1;
  L2Controller m_l2;
  // Память кэша
  detail::SimpleMemory m_mem;
  detail::CacheAsLowerLevel<L2Controller> m_l2_as_lower;

  // Функции отправления запроса и ответа в нижний кэш
  std::function<void(const Request&)> m_to_l2;
  std::function<Response()> m_from_l2;

public:
  CacheSystem(size_t l1_sets, size_t l1_assoc, size_t l1_block,
              size_t l2_sets, size_t l2_assoc, size_t l2_block)
    : m_l1(l1_sets, l1_assoc, l1_block)
    , m_l2(l2_sets, l2_assoc, l2_block)
    , m_mem(l2_block)
    , m_l2_as_lower(m_l2,
        [this](const Request& req) { m_mem(req); },
        [this]() { return m_mem.get_response(); })
  {
    // Функции связи кэшей
    m_to_l2  = [this](const Request& req) { m_l2_as_lower(req); };
    m_from_l2 = [this]() { return m_l2_as_lower.get_response(); };

    std::clog << "CacheSystem created:\n"
              << "  L1: " << l1_sets << " sets × " << l1_assoc << "-way, " << l1_block << "B\n"
              << "  L2: " << l2_sets << " sets × " << l2_assoc << "-way, " << l2_block << "B\n";
  }

  // Главный метод запрос от процессора (возвращает ответ) верхнему кэшу
  Response process(const Request& req) {
    return m_l1.process_transaction(req, m_to_l2, m_from_l2);
  }

  // Для проверки мб
  size_t l1_block_size() const noexcept { return m_l1.get_block_size(); }
  size_t l2_block_size() const noexcept { return m_l2.get_block_size(); }
};

} // namespace cache
