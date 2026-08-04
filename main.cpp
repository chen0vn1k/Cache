#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <random>
#include <cstdint>

#include <boost/program_options.hpp>

#include "cache_system.hpp"

// Заполнение памяти (100 блоков)
static void seed_memory(cache::SimpleMemory& mem, size_t nblocks, uint64_t base, uint32_t seed) {
  (void)seed;
  
  const size_t bs = mem.block_size();
  const size_t actual_blocks = std::min(nblocks, size_t(100));
  
  for (size_t i = 0; i < actual_blocks; ++i) {
    std::vector<std::byte> data(bs);
    // Заполнение блоков
    for (size_t b = 0; b < bs; ++b) {
      data[b] = static_cast<std::byte>(i & 0xFF);
    }
    mem.seed(base + i * bs, std::move(data));
  }
  
  std::clog << "[INIT] seeded " << actual_blocks << " blocks into MEM from 0x"
            << std::hex << base << std::dec << "\n";
}

static void run(std::istream& in, cache::Cache& l1) {
  std::string line;
  size_t tick = 1;

  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;

    std::istringstream iss(line);
    char op;
    uint64_t addr;
    if (!(iss >> op >> std::hex >> addr)) continue;

    std::clog << "\n--- Request " << tick++ << " ---\n";

    if (op == 'R' || op == 'r') {
      size_t size = 4;
      iss >> std::dec >> size;
      if (size == 0) size = 4;

      std::clog << "[CPU → L1] READ  0x" << std::hex << addr << std::dec
                << " sz=" << size << "\n";

      auto resp = l1.process(cache::ReadRequest{addr, size});
      auto& data = std::get<cache::ReadResponse>(resp).data;
      std::clog << "[L1 → CPU] READ  " << data.size() << " bytes\n";
    }
    else if (op == 'W' || op == 'w') {
      std::vector<std::byte> data;
      unsigned v;
      while (iss >> std::hex >> v)
        data.push_back(static_cast<std::byte>(v & 0xFF));
      if (data.empty()) data.push_back(std::byte{0});

      std::clog << "[CPU → L1] WRITE 0x" << std::hex << addr << std::dec
                << " sz=" << data.size() << "\n";

      auto resp = l1.process(cache::WriteRequest{addr, std::move(data)});
      bool ok = std::get<cache::WriteResponse>(resp).success;
      std::clog << "[L1 → CPU] WRITE " << (ok ? "OK" : "FAIL") << "\n";
    }
  }
  std::clog << "\n=== DONE ===\n";
}

int main(int argc, char* argv[]) {
  namespace po = boost::program_options;

  po::options_description desc("options");
  desc.add_options()
    ("help,h", "help")
    ("trace,t", po::value<std::string>(), "trace file")
    ("fill,f", po::bool_switch()->default_value(false), "seed random blocks into MEM");

  po::variables_map vm;
  try {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
  if (vm.count("help")) {
    std::cout << desc << "\n";
    return 0;
  }
  
  // =============== ОБЪЯВЛЕНИЕ кэша =======================


  auto l1 = cache::make_cache(4, 2, 64);
  auto buffer = cache::make_cache<cache::LRUPolicy,
                                  cache::WriteThroughPolicy,
                                  cache::NoReadAllocatePolicy>(8, 4, 64);
  auto l2 = cache::make_cache<cache::FIFOPolicy,
                              cache::WriteThroughPolicy,
                              cache::AllAllocatePolicy>(8, 4, 64);
  auto mem = cache::SimpleMemory(64);

  l1.set_name("L1");
  buffer.set_name("BUFFER");
  l2.set_name("L2");
  mem.set_name("MEM");
  // Заполнение слево направо как по иерархии кэшей сверху вниз
  cache::link_hierarchy({&l1, &buffer, &l2}, &mem);

  // ======================================================

  // Заполнение
  if (vm["fill"].as<bool>()) {
    uint64_t base = 0;
    std::istringstream(vm["fill-base"].as<std::string>()) >> std::hex >> base;
    seed_memory(mem, vm["fill-blocks"].as<size_t>(), base, vm["fill-seed"].as<uint32_t>());
  }

  if (vm.count("trace")) {
    std::ifstream f(vm["trace"].as<std::string>());
    if (!f) {
      std::cerr << "cannot open trace\n";
      return 1;
    }
    run(f, l1);
  } else {
    std::clog << "stdin (Ctrl+D to end)\n";
    run(std::cin, l1);
  }
  return 0;
}
