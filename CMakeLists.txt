cmake_minimum_required(VERSION 3.20)
project(gamecatcher VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Windows: runtime estático (combina com o triplet x64-windows-static do vcpkg), .exe sem DLLs.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

find_package(CURL REQUIRED)

find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)
endif()

# Monocypher: verificação Ed25519 das atualizações (2 arquivos C, domínio público / BSD-2).
FetchContent_Declare(monocypher
  URL https://monocypher.org/download/monocypher-4.0.2.tar.gz
  URL_HASH SHA256=38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(monocypher)
add_library(monocypher STATIC
  ${monocypher_SOURCE_DIR}/src/monocypher.c
  ${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c)
target_include_directories(monocypher PUBLIC
  ${monocypher_SOURCE_DIR}/src
  ${monocypher_SOURCE_DIR}/src/optional)

add_executable(gamecatcher
  src/main.cpp
  src/http.cpp
  src/store.cpp
  src/seen_db.cpp
  src/install.cpp
  src/update.cpp
  src/steam_local.cpp
  src/notify_common.cpp)

if(WIN32)
  # Toast via C++/WinRT (headers do Windows SDK).
  target_sources(gamecatcher PRIVATE src/notify_windows.cpp)
  target_compile_definitions(gamecatcher PRIVATE NOMINMAX UNICODE _UNICODE)
  target_link_libraries(gamecatcher PRIVATE windowsapp shell32 advapi32)
else()
  # Notificações via D-Bus (sd-bus da libsystemd).
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
  target_sources(gamecatcher PRIVATE src/notify_linux.cpp)
  target_link_libraries(gamecatcher PRIVATE PkgConfig::SYSTEMD)
endif()

target_link_libraries(gamecatcher PRIVATE CURL::libcurl nlohmann_json::nlohmann_json monocypher)
target_compile_definitions(gamecatcher PRIVATE GAMECATCHER_VERSION="${PROJECT_VERSION}")

if(MSVC)
  target_compile_options(gamecatcher PRIVATE /W4 /utf-8 /permissive-)
else()
  target_compile_options(gamecatcher PRIVATE -Wall -Wextra -Wpedantic)
endif()
