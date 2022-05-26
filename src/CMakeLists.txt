cmake_minimum_required(VERSION 3.20)

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

add_executable(
  server "${CMAKE_CURRENT_BINARY_DIR}/include/res.cpp" "server.cpp"
         "message.cpp" "vocabserv.cpp" "format.cpp" "buffer.cpp")
target_include_directories(
  server PRIVATE "${CONAN_INCLUDE_DIRS}" "${PROJECT_SOURCE_DIR}/include"
                 "${CMAKE_CURRENT_BINARY_DIR}/include")
target_link_libraries(server PRIVATE ${CONAN_LIBS})
