cmake_minimum_required(VERSION 3.16)

# Enforce Modern C++ Standards
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Forces standard C++ (no gnu++20)

list(APPEND EXTRA_COMPONENT_DIRS "components/bluetooth")

# Automatically merge local credentials if the file exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults.local")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(selective_pet_access)
