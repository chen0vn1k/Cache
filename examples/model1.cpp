#include "cache_system.hpp"

cache::Hierarchy build_model()
{
  auto l1  = cache::make_cache<cache::LRUPolicy,
                               cache::WriteBackPolicy,
                               cache::AllAllocatePolicy>("L1", 4, 2, 64);

  auto buf = cache::make_cache<cache::LRUPolicy,
                               cache::WriteThroughPolicy,
                               cache::NoReadAllocatePolicy>("BUF", 8, 4, 64);

  auto l2  = cache::make_cache<cache::FIFOPolicy,
                               cache::WriteThroughPolicy,
                               cache::AllAllocatePolicy>("L2", 8, 4, 64);
  
  auto mem = cache::make_memory("MEM", 64);

  // Слева направо: L1 → BUF → L2 → MEM
  return cache::make_hierarchy(l1, buf, l2, mem);
}
