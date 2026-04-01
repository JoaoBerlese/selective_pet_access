cmake_minimum_required(VERSION 3.16)

# Enforce Modern C++ Standards
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Forces standard C++ (no gnu++20)

list(APPEND EXTRA_COMPONENT_DIRS "components/bluetooth")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(selective_pet_access)
