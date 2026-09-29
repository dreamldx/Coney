cmake_minimum_required(VERSION 3.16)
project(coney C)

set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# LVGL reads its configuration from our own lv_conf.h
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)

# EXCLUDE_FROM_ALL: only build the lvgl library itself, not its examples and demos
add_subdirectory(third_party/lvgl EXCLUDE_FROM_ALL)

add_executable(coney
  src/main.c
  src/whisplay_display.c
)
target_include_directories(coney PRIVATE src)
target_compile_options(coney PRIVATE -Wall -Wextra)
target_link_libraries(coney PRIVATE lvgl m pthread)
