cmake_minimum_required(VERSION 3.10)
project(CacheModel CXX)

# Отключаем новую политику
cmake_policy(SET CMP0167 OLD)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Флаги компиляции
add_compile_options(-Wall -Werror -pedantic)

# Заголовки под include/
add_library(cache_lib INTERFACE)
target_include_directories(cache_lib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(Boost CONFIG REQUIRED COMPONENTS program_options log log_setup)

# Проверяем что нашли
if(NOT Boost_FOUND)
    message(FATAL_ERROR "Boost not found! Please install Boost.")
endif()

add_compile_definitions(CACHE_ENABLE_LOG=1)


# Собираем примеры
file(GLOB MODEL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp")

# Для каждого тестового файла создаем отдельный исполняемый файл
foreach(MODEL_FILE ${MODEL_FILES})
    message(INFO "MODEL: $(MODEL_FILE)")
    get_filename_component(MODEL_NAME ${MODEL_FILE} NAME_WE)
    
    add_executable(${MODEL_NAME} 
        main.cpp 
        ${MODEL_FILE}
        src/trace_logger.cpp
    )

    target_link_libraries(${MODEL_NAME} 
        PRIVATE 
            cache_lib 
            Boost::program_options
            Boost::log
            Boost::log_setup
    )

    if(TARGET Boost::thread)
      target_link_libraries(${MODEL_NAME} PRIVATE Boost::thread)
    endif()

    install(TARGETS ${MODEL_NAME} RUNTIME DESTINATION bin)
    
endforeach()
