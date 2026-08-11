cmake_minimum_required(VERSION 3.20)
project(CacheModel CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Подключение трассировки
option(CACHE_ENABLE_LOG "Enable cache hierarchy tracing " ON)

# Флаги компиляции
add_compile_options(-Wall -Werror -pedantic)

# Подключаем только главные 3 заголовочных файла
add_library(cache_lib INTERFACE)
target_include_directories(cache_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Находим Boost (общая часть)
find_package(Boost REQUIRED COMPONENTS program_options)

# Если трассировка включена — ищем дополнительные компоненты Boost
if(CACHE_ENABLE_LOG)
    find_package(Boost REQUIRED COMPONENTS log log_setup filesystem)
endif()

# Собираем все *.cpp файлы (модели кэшей)
# ============================================
file(GLOB MODEL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp")

# Для каждого тестового файла создаем отдельный исполняемый файл
foreach(MODEL_FILE ${MODEL_FILES})
    get_filename_component(MODEL_NAME ${MODEL_FILE} NAME_WE)
    
    add_executable(${MODEL_NAME} 
        main.cpp 
        ${MODEL_FILE}
    )
    
    # Линкуем библиотеки
    target_link_libraries(${MODEL_NAME} 
        PRIVATE 
            cache_lib 
            Boost::program_options
    )
    
    # Если трассировка включена
    if(CACHE_ENABLE_LOG)
        target_compile_definitions(${MODEL_NAME} PRIVATE
            CACHE_ENABLE_LOG=1
            BOOST_LOG_DYN_LINK
        )
        target_link_libraries(${MODEL_NAME} PRIVATE
            Boost::log
            Boost::log_setup
            Boost::filesystem
        )
    else()
        target_compile_definitions(${MODEL_NAME} PRIVATE CACHE_ENABLE_LOG=0)
    endif()
endforeach()
