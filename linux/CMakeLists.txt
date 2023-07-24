CMAKE_MINIMUM_REQUIRED(VERSION 3.5 FATAL_ERROR)

project(SynchClientServer_Linux_64)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fvisibility-inlines-hidden")

set (ROOT_DIR ${CMAKE_SOURCE_DIR}/..)
set (CMAKE_SOURCE_DIR ${ROOT_DIR}/src)

find_package(Boost REQUIRED)
find_package(RapidJSON REQUIRED)

set(Boost_USE_MULTITHREADED ON)

SET(AddInNative_SRC
    ${CMAKE_SOURCE_DIR}/JsonProsessing.cpp
    ${CMAKE_SOURCE_DIR}/SynchServer.cpp
    ${CMAKE_SOURCE_DIR}/AddInNative.cpp
    )

include_directories(${CMAKE_SOURCE_DIR} ${Boost_INCLUDE_DIRS} ${RapidJSON_INCLUDE_DIR})

add_library(${PROJECT_NAME} SHARED ${AddInNative_SRC})

target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES} -pthread -ldl -lboost_thread -lboost_date_time)