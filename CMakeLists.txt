cmake_minimum_required(VERSION 3.18)
project(mod_audio_stream
        VERSION 0.0.1
        DESCRIPTION "Audio streaming module for FreeSWITCH."
        HOMEPAGE_URL "https://github.com/amigniter/mod_audio_stream")

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")

option(ENABLE_LOCAL "Enable local compile/debug specific" OFF)
if(ENABLE_LOCAL)
    set(ENV{PKG_CONFIG_PATH} "/usr/local/freeswitch/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(PkgConfig REQUIRED)
find_package(SpeexDSP REQUIRED)
find_package(OpenSSL REQUIRED)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)

pkg_check_modules(FreeSWITCH REQUIRED IMPORTED_TARGET freeswitch)
pkg_get_variable(FS_MOD_DIR freeswitch modulesdir)
message(STATUS "FreeSWITCH modules dir: ${FS_MOD_DIR}")

add_library(mod_audio_stream SHARED 
    mod_audio_stream.c
    mod_audio_stream.h
    audio_streamer_glue.h
    audio_streamer_glue.cpp
    ws_client.h
    ws_client.cpp
    stream_utils.h
    stream_utils.cpp
    base64.cpp
)

set_property(TARGET mod_audio_stream PROPERTY POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(mod_audio_stream PRIVATE USE_TLS)

target_link_libraries(mod_audio_stream PRIVATE 
    PkgConfig::FreeSWITCH 
    pthread
    OpenSSL::SSL
    OpenSSL::Crypto
    PkgConfig::OPUS
)

if(CMAKE_BUILD_TYPE MATCHES "Release")
    set_target_properties(${PROJECT_NAME} 
        PROPERTIES 
        LINK_FLAGS_RELEASE "-s")
endif()

install(TARGETS ${PROJECT_NAME}
        DESTINATION ${FS_MOD_DIR})
