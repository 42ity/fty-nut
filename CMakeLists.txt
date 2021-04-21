cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

##############################################################################################################

project(fty-nut VERSION 1.0.0 LANGUAGES CXX)

########################################################################################################################

find_package(fty-cmake PATHS ${CMAKE_BINARY_DIR})

########################################################################################################################

add_subdirectory(lib)
#add_subdirectory(agent)

## agent configuration
## https://cmake.org/cmake/help/v3.0/module/GNUInstallDirs.html
