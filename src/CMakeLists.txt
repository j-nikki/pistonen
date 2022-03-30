cmake_minimum_required(VERSION 3.20)

find_path(BOOST_PREPROCESSOR_INCLUDE_DIR NAMES "boost/preprocessor.hpp" REQUIRED)
find_package(range-v3 CONFIG REQUIRED)

add_executable(pistonen-test test.cpp)

set_property(TARGET pistonen-test PROPERTY CXX_STANDARD 23)

target_include_directories(pistonen-test PRIVATE ${BOOST_PREPROCESSOR_INCLUDE_DIR})
target_link_libraries(pistonen-test PRIVATE range-v3 range-v3-meta range-v3::meta range-v3-concepts)