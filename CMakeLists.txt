cmake_minimum_required(VERSION 3.20)
project(CacheModel CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Флаги компиляции
add_compile_options(-Wall -Werror)
# Подключаем только главные 3 заголовочных файла
add_library(cache_lib INTERFACE)
target_include_directories(cache_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Boost CONFIG REQUIRED COMPONENTS program_options)

add_executable(test_bench main.cpp)

target_link_libraries(test_bench 
    PRIVATE 
        cache_lib 
        Boost::program_options
)

# Флаги компиляции для test_bench
#target_compile_options(test_bench PRIVATE -Wall -Werror)
