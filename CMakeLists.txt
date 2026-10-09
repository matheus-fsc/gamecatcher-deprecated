cmake_minimum_required(VERSION 3.20)
project(gamecatcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

find_package(CURL REQUIRED)

find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)
endif()

# nayuki QR Code generator (single .cpp/.hpp)
FetchContent_Declare(qrcodegen
  GIT_REPOSITORY https://github.com/nayuki/QR-Code-generator.git
  GIT_TAG v1.8.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(qrcodegen) # repo sem CMakeLists na raiz: só baixa
add_library(qrcodegen STATIC ${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp)
target_include_directories(qrcodegen PUBLIC ${qrcodegen_SOURCE_DIR}/cpp)

add_executable(gamecatcher
  src/main.cpp
  src/http.cpp
  src/auth.cpp
  src/token_store.cpp
  src/store.cpp)

target_link_libraries(gamecatcher PRIVATE CURL::libcurl nlohmann_json::nlohmann_json qrcodegen)

if(MSVC)
  target_compile_options(gamecatcher PRIVATE /W4 /utf-8)
else()
  target_compile_options(gamecatcher PRIVATE -Wall -Wextra -Wpedantic)
endif()
