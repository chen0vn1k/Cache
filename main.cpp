#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <iomanip>

#include <boost/program_options.hpp>

#include "include/cache_system.hpp"

void run_simulation(std::istream& in) {
  // L1: 4 sets × 2-way, 64B
  // L2: 8 sets × 4-way, 64B
  cache::CacheSystem<> cache(4, 2, 64, 8, 4, 64);

  std::string line;
  size_t tick = 1;

  std::clog << "L1: 4 sets × 2-way, 64B | LRU + Write-Back\n";
  std::clog << "L2: 8 sets × 4-way, 64B | FIFO + Write-Through\n";

  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;

    std::istringstream iss(line);
    char op_char;
    uint64_t addr;

    if (!(iss >> op_char >> std::hex >> addr)) continue;

    std::clog << "\n--- Request " << tick++ << " ---\n";

    cache::Request req;
    bool is_write = false;

    if (op_char == 'R' || op_char == 'r') {
      size_t size = 0;
      iss >> std::dec >> size;
      if (size == 0) size = 4;

      req = cache::ReadRequest{addr, size};

      std::clog << "[CPU → L1] READ  addr: 0x" << std::hex << addr << std::dec
                << " | size: " << size << " bytes\n";
    }
    else if (op_char == 'W' || op_char == 'w') {
      is_write = true;
      std::vector<std::byte> data;
      unsigned int byte_val;

      while (iss >> std::hex >> byte_val) {
        data.push_back(static_cast<std::byte>(byte_val & 0xFF));
      }
      if (data.empty()) data.push_back(std::byte{0});

      req = cache::WriteRequest{addr, std::move(data)};

      std::clog << "[CPU → L1] WRITE addr: 0x" << std::hex << addr << std::dec
                << " | size: " << std::get<cache::WriteRequest>(req).data.size() << " bytes\n";
    }
    else {
      std::cerr << "Unknown operation: " << op_char << "\n";
      continue;
    }

    // Отправляем запрос в систему
    auto resp = cache.process(req);

    // Ответ
    if (is_write) {
      bool ok = std::get<cache::WriteResponse>(resp).success;
      std::clog << "[L1 → CPU] WRITE " << (ok ? "OK" : "FAILED") << "\n";
    } else {
      auto& data = std::get<cache::ReadResponse>(resp).data;
      std::clog << "[L1 → CPU] READ  returned " << data.size() << " bytes\n";
    }
  }

  std::clog << "\n=== SIMULATION FINISHED ===\n";
}

int main(int argc, char* argv[]) {
  namespace po = boost::program_options;

  po::options_description desc("Allowed options");
  desc.add_options()
    ("help,h", "показать справку")
    ("trace,t", po::value<std::string>(),
     "путь к файлу трассы (если не указан — читаем из stdin)");

  po::variables_map vm;
  try {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
  } catch (const std::exception& e) {
    std::cerr << "CLI Error: " << e.what() << "\n";
    return 1;
  }

  if (vm.count("help")) {
    std::cout << desc << "\n";
    return 0;
  }

  if (vm.count("trace")) {
    std::string file_path = vm["trace"].as<std::string>();
    std::ifstream trace_file(file_path);

    if (!trace_file.is_open()) {
      std::cerr << "Error: Could not open trace file: " << file_path << "\n";
      return 1;
    }

    std::clog << "Using trace file: " << file_path << "\n";
    run_simulation(trace_file);
  } else {
    std::clog << "No trace file provided. Reading from stdin (Ctrl+D to finish)...\n";
    run_simulation(std::cin);
  }

  return 0;
}
