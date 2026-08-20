cmake_minimum_required(VERSION 3.28)

project(obs-virtual-audio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(libobs REQUIRED)

add_library(${PROJECT_NAME} MODULE
    src/plugin-main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PLUGIN_VERSION="${PROJECT_VERSION}"
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
endif()
