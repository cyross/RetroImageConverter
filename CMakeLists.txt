# 補助用 CMake (コアライブラリ・CLI・テストのビルド確認用)
# Windows での開発は RetroImageConv.sln (Visual Studio 2026) を使用してください。
cmake_minimum_required(VERSION 3.16)
project(RetroImageConv CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8 /W4)
endif()

file(GLOB CORE_SRC src/core/*.cpp)
add_library(RetroImageCore STATIC ${CORE_SRC})
target_include_directories(RetroImageCore PUBLIC src/core)
if(WIN32)
  target_link_libraries(RetroImageCore PUBLIC windowscodecs ole32)
endif()

if(WIN32)
  add_executable(RetroImageConvCLI src/cli/main.cpp src/cli/RetroImageConvCLI.rc)
else()
  add_executable(RetroImageConvCLI src/cli/main.cpp)
endif()
target_link_libraries(RetroImageConvCLI PRIVATE RetroImageCore)

add_executable(RetroImageCoreTests tests/core_tests.cpp)
target_link_libraries(RetroImageCoreTests PRIVATE RetroImageCore)

if(WIN32)
  add_executable(RetroImageConv WIN32 src/gui/MainWindow.cpp src/gui/RetroImageConv.rc)
  target_link_libraries(RetroImageConv PRIVATE RetroImageCore comctl32 comdlg32 shell32 gdi32 user32 msimg32)
endif()
