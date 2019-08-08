################################################################################
# Minimal CMake file to compile an MPI program

cmake_minimum_required(VERSION 2.8)

project(random_block)

# find pthreads
find_package(Threads REQUIRED)

# find MPI
find_package(MPI REQUIRED)

# Don't use C++ bindings.
add_definitions(-DOMPI_SKIP_MPICXX)

# includes
include_directories(SYSTEM ${MPI_INCLUDE_PATH})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# sources
add_executable(random_block random_block.cpp thread_pool.cpp)

target_link_libraries(random_block
  ${MPI_C_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

################################################################################
