#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <iomanip>

#include <boost/program_options.hpp>

// Пользовательский интерфейс нашей библиотеки
#include <cache_system.hpp>

namespace po = boost::program_options;

// Параметризация модели кэш-памяти
constexpr cache::CacheConfig TEST_CONFIG {
  .cache_size = 1024,
  .block_size = 64,
  .associativity = 2,
  .replacement_policy = cache::ReplacementPolicy::LRU,
  .write_policy = cache::WritePolicy::WRITE_BACK
};

// Функция, прогоняющая трассу через модель кэша
void run_simulation(std::istream& in) {
  cache::CacheSystem<TEST_CONFIG> cache_model;
  std::string line;
  size_t tick = 1;

  // Лямбда-функция для обработки запроса кэш -> память
  auto to_mem = [](const cache::MemRequest& req) {
    std::string op = (req.op_type == cache::OperationType::READ) ? "READ" : "WRITE";
    std::clog << "  [Cache to Mem] " << op << " addr: 0x" 
              << std::hex << req.address << std::dec 
              << " | size: " << req.data.size() << " bytes\n";
  };

  // Лямбда-функция для ответа память - кэш
  auto from_mem = []() -> cache::MemResponse {
    std::clog << "  [Mem to Cache] Block returned (" << TEST_CONFIG.block_size << " bytes)\n";
    // Возвращаем блок данных заполненный нулями
    return {true, std::vector<std::byte>(TEST_CONFIG.block_size, std::byte{0})};
  };

  std::clog << "=== STARTING CACHE SIMULATION ===\n";

  // Построчное чтение трассы
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;

    std::istringstream iss(line);
    char op_char;
    uint64_t addr;
    
    // Считываем тип операции и адрес (в hex)
    if (!(iss >> op_char >> std::hex >> addr)) continue;

    cache::CpuRequest req;
    req.address = addr;

    std::clog << "\n--- Request " << tick++ << " ---\n";

    // обработка запроса на чтение от процессора
    if (op_char == 'R' || op_char == 'r') {
      size_t size;
      iss >> std::dec >> size;
      req.op_type = cache::OperationType::READ;
      req.data.resize(size);
      
      std::clog << "[CPU to Cache] READ addr: 0x" << std::hex << addr << std::dec 
                << " | size: " << size << " bytes\n";
    } 
    // Обработка запроса на запись от процессора
    else if (op_char == 'W' || op_char == 'w') {
      req.op_type = cache::OperationType::WRITE;
      unsigned int byte_val;
      
      // Считываем байты данных для записи
      while (iss >> std::hex >> byte_val) {
        req.data.push_back(static_cast<std::byte>(byte_val));
      }
      std::clog << "[CPU to Cache] WRITE addr: 0x" << std::hex << addr << std::dec 
                << " | size: " << req.data.size() << " bytes\n";
    } 
    else {
      // Для непонятных запросов
      std::cerr << "Unknown operation: " << op_char << "\n";
      continue;
    }

    // Подача запроса
    cache_model.send_cpu_request(req);
    
    // выполнение работы контроллера (если промах — вызовутся to_mem и from_mem)
    cache_model.execute(to_mem, from_mem);

    // Получение ответа от кэша
    auto resp = cache_model.get_cpu_response();
    if (resp.has_value()) {
      std::clog << "[Cache to CPU] Hit: " << (resp->hit ? "TRUE" : "FALSE") << "\n";
    }
  }
  
  std::clog << "\n=== SIMULATION FINISHED ===\n";
}

int main(int argc, char* argv[]) {
  // Настройка парсера аргументов (для старта программы)
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help,h", "это мануал")
    ("trace,t", po::value<std::string>(), "это путь к файлу трассы, если его не указать то будет работать ввод из терминала");

  // это для проверки правильности аргументов
  po::variables_map vm;
  try {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
  } catch (const std::exception& e) {
    std::cerr << "CLI Error: " << e.what() << "\n";
    return 1;
  }

  // Обработка флага помощи
  if (vm.count("help")) {
    std::cout << desc << "\n";
    return 0;
  }

  // Обработка флага адреса файла трассы
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
    std::clog << "No trace file provided. Reading from stdin (Press Ctrl+D to finish)...\n";
    run_simulation(std::cin);
  }

  return 0;
}
