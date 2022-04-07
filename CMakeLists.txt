cmake_minimum_required(VERSION 3.16)
project(http-demo)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost COMPONENTS system thread)
find_package(Threads)

add_executable(http-demo main.cpp)

target_link_libraries(http-demo pthread Boost::boost Boost::system Boost::thread Threads::Threads)


