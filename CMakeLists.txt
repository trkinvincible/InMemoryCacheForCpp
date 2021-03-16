project(lock_free_cache)

cmake_minimum_required(VERSION 2.8)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if(COMPILER_SUPPORTS_MARCH_NATIVE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
endif()
set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_CXX_FLAGS "-pg") - use only with GCC 's own profiler

find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

SET( _SOURCES_

    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

SET( _HEADER_

    ${CMAKE_CURRENT_SOURCE_DIR}/cachemanager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/command.h
    ${CMAKE_CURRENT_SOURCE_DIR}/writer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/reader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fileutility.h
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h
    ${CMAKE_CURRENT_SOURCE_DIR}/utilstructs.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gtest.h
)

find_package(Boost COMPONENTS program_options REQUIRED)

######################
#Include Definitions #
######################
#add_definitions(-DUSING_BOOST_IPC)

######################
#Include Directories #
######################
include_directories(${Boost_INCLUDE_DIRS})

######################
#link Directories    #
######################
link_directories()

######################
# Create executable  #
######################
add_executable(${PROJECT_NAME} ${_SOURCES_} ${_HEADER_})

target_link_libraries(${PROJECT_NAME}
    -lpthread
    -ltcmalloc
    -latomic
    -Wl,--no-as-needed -lprofiler
    -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free
    ${Boost_LIBRARIES}
    ${GTEST_LIBRARIES}
)








