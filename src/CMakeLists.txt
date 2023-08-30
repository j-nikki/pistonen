cmake_minimum_required(VERSION 3.20)

# find_package(jutil REQUIRED)

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
# gen cert.inl
#

set(INCLDIR "${CMAKE_CURRENT_BINARY_DIR}/include")
add_custom_command(
    OUTPUT "${INCLDIR}/cert.inl"
    COMMAND mkdir -p "${INCLDIR}" && ../scripts/gen_cert.sh > "${INCLDIR}/cert.inl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/gen_cert.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating cert.inl"
    VERBATIM
)

#
# server target
#

option(PNEN_TRANSCODE "Whether to transcode static video files" OFF)

# find_package(LibreSSL CONFIG REQUIRED)
#find_package(WolfSSL CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(SQLite3 CONFIG REQUIRED)
find_package(dragonbox CONFIG REQUIRED)
add_executable(
  server "${INCLDIR}/cert.inl" "${CMAKE_CURRENT_BINARY_DIR}/include/res.cpp" server.cpp serve.cpp # "ptf/writer.cpp" "ptf/ptf.cpp" "ptf/table.cpp"
         message.cpp vocabserv.cpp format.cpp buffer.cpp log.cpp custom_serve.cpp) # ptf/detail/grammar.cpp
target_include_directories(server PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/include")
#target_include_directories(
#  server PRIVATE
#  "${robin_hood_INCLUDE_DIRS}"
#  "${LibreSSL_INCLUDE_DIRS}"
#  "${magic_enum_INCLUDE_DIRS}"
#  #"${PROJECT_SOURCE_DIR}/include"
#  "${CMAKE_CURRENT_BINARY_DIR}/include")
# target_compile_options(server PRIVATE -lkeyutils)
target_link_libraries(server jutil wolfssl $<$<BOOL:${PNEN_TRANSCODE}>:avformat avcodec avfilter avutil postproc swscale swresample avdevice> magic_enum::magic_enum sqlite3 dragonbox::dragonbox)
target_compile_definitions(server PRIVATE TIMESTAMP_DIR="front" PNEN_SERVER_TASK=serve PNEN_SSL PNEN_KEEP_ALIVE=3 $<$<BOOL:${PNEN_TRANSCODE}>:PNEN_TRANSCODE>)
