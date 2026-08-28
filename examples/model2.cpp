#include "cache_system.hpp"

cache::Hierarchy build_model() {
  auto l1  = cache::make_cache<cache::LRUPolicy,
                               cache::WriteThroughPolicy,
                               cache::AllAllocatePolicy>("L1", 4, 2, 64);
  auto mem = cache::make_memory("MEM", 64);

  // Слева направо: L1 > MEM
  return cache::make_hierarchy(l1, mem);
}

