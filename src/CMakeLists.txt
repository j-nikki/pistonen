cmake_minimum_required(VERSION 3.20)

#
# dependencies (available on, e.g., vcpkg)
#

find_package(magic_enum CONFIG REQUIRED)
find_path(BOOST_PREPROCESSOR_INCLUDE_DIR NAMES "boost/preprocessor.hpp" REQUIRED)
find_path(BOOST_ITERATOR_INCLUDE_DIR NAMES "boost/iterator.hpp" REQUIRED)
find_path(BOOST_VARIANT2_INCLUDE_DIR NAMES "boost/variant2.hpp" REQUIRED)
find_package(range-v3 CONFIG REQUIRED)
# find_package(robin_hood CONFIG REQUIRED)
# find_package(pegtl CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

#
# populate ${CMAKE_CURRENT_BINARY_DIR}/include
#

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include")
find_package(Python COMPONENTS Interpreter)
set(resfiles "res/index.html" "res/reset.css" "res/index.css")
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/res.h"
         "${CMAKE_CURRENT_BINARY_DIR}/include/res.cpp"
  COMMAND "${Python_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/resgen.py"
          $<CONFIG:Debug> "${CMAKE_CURRENT_BINARY_DIR}/include" ${resfiles}
  DEPENDS "${PROJECT_SOURCE_DIR}/scripts/resgen.py" ${resfiles}
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
  VERBATIM)

#
# server target
#

add_executable(server "${CMAKE_CURRENT_BINARY_DIR}/include/res.cpp" "server.cpp" "message.cpp" "vocabserv.cpp" "format.cpp" "buffer.cpp")

target_include_directories(
  server
  PRIVATE "${BOOST_VARIANT2_INCLUDE_DIR}" "${BOOST_PREPROCESSOR_INCLUDE_DIR}" "${BOOST_ITERATOR_INCLUDE_DIR}" "${PROJECT_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/include")
target_link_libraries(server PRIVATE magic_enum::magic_enum OpenSSL::SSL OpenSSL::Crypto)
