#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <random>
#include <algorithm>

#include <boost/program_options.hpp>

#include "cache_system.hpp"
#include "cache/detail/trace_logger.hpp"

// Определено в example/*.cpp
cache::Hierarchy build_model();


static void seed_memory(cache::SimpleMemory& mem)
{
  const size_t bs = mem.block_size();
  const size_t nblocks = 16;

  std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<unsigned> dist(0, 255);

  for (size_t i = 0; i < nblocks; ++i)
  {
    std::vector<std::byte> data(bs);
    std::generate(data.begin(), data.end(), 
                  [&]
                  {
                    return static_cast<std::byte>(dist(rng));
                  });
    mem.seed(i * bs, std::move(data));
  }

  std::clog << "[INIT] seeded " << nblocks 
            << " blocks with random data into MEM\n";
}

static void run_trace(std::istream& in, cache::Hierarchy& sys)
{
  std::string line;
  size_t tick = 1;

  while (std::getline(in, line))
  {
    if (line.empty() || line[0] == '#')
    {
      continue;
    }

    std::istringstream iss(line);
    char op;
    uint64_t addr;
    if (!(iss >> op >> std::hex >> addr))
    {
      std::cerr << "[WARN] bad trace line " << tick << ": " << line << "\n";
      continue;
    }

    std::clog << "\n--- Request " << tick++ << " ---\n";

    if (op == 'R' || op == 'r')
    {
      size_t size = 4;
      iss >> std::dec >> size;
      if (size == 0)
      {
        size = 4;
      }

      auto resp = sys.process(cache::ReadRequest{addr, size});
      std::get<cache::ReadResponse>(resp);
    }
    else if (op == 'W' || op == 'w')
    {
      std::vector<std::byte> data;
      unsigned v;
      while (iss >> std::hex >> v)
      {
        data.push_back(static_cast<std::byte>(v & 0xFF));
      }
      if (data.empty())
      {
        data.push_back(std::byte{0});
      }

      sys.process(cache::WriteRequest{addr, std::move(data)});
    }
    else
    {
      std::cerr << "[WARN] unknown operation '" << op << "' at line " << tick - 1 << "\n";
    }
  }
  std::clog << "\n=== DONE ===\n";
}

int main(int argc, char* argv[])
{
  namespace po = boost::program_options;

  po::options_description desc("options");
  desc.add_options()
    ("help,h",    "help")
    ("trace,t",   po::value<std::string>(),  "trace file")
    ("fill,f",    po::bool_switch()->default_value(false), "seed random blocks into MEM")
    ("dump,d",    po::value<std::string>(),  "dump final cache/memory state to directory (by block key)");

  po::variables_map vm;

  // Обработка аргументов
  try
  {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
  }
  catch (const po::error& e)
  {
    std::cerr << "error: " << e.what() << "\n\n" << desc << "\n";
    return 1;
  }

  // Справка
  if (vm.count("help"))
  {
    std::cout << desc << "\n";
    return 0;
  }

  // Инициализация логирования
  cache::log::TraceLogger::init();

  auto sys = build_model();

  // Заполнение памяти
  if (vm["fill"].as<bool>())
  {
    seed_memory(sys.mem());
  }

  // Обработка трассы
  if (vm.count("trace"))
  {
    const std::string path = vm["trace"].as<std::string>();
    std::ifstream f(path);
    if (!f)
    {
      std::cerr << "error: cannot open trace file: " << path << "\n";
      return 1;
    }
    run_trace(f, sys);
  }
  else
  {
    std::clog << "reading from stdin (Ctrl+D to end)\n";
    run_trace(std::cin, sys);
  }

  // Выдача состояния по ключу запуска --dump <dir>
  if (vm.count("dump"))
  {
    const std::string dir = vm["dump"].as<std::string>();
    try
    {
      sys.dump_state(dir);
      std::clog << "[DUMP] state written to " << dir << "/\n";
    }
    catch (const std::exception& e)
    {
      std::cerr << "error: dump failed: " << e.what() << "\n";
      return 1;
    }
  }

  return 0;
}
