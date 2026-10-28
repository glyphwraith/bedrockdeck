cmake_minimum_required(VERSION 3.15)
project(bedrockdeck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# Link the MSVC runtime statically so the .exe runs on any Windows PC
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(bedrockdeck src/main.cpp)
target_include_directories(bedrockdeck PRIVATE include)

if(WIN32)
  target_link_libraries(bedrockdeck PRIVATE comctl32 comdlg32 shell32 ole32 user32 gdi32 advapi32)
endif()
if(MSVC)
  # a windowed application: double-clicking shows the window and no console
  set_target_properties(bedrockdeck PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
  target_compile_options(bedrockdeck PRIVATE /utf-8 /W3)
  target_compile_definitions(bedrockdeck PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  if(WIN32)
    target_link_options(bedrockdeck PRIVATE -mwindows)
  endif()
  target_compile_options(bedrockdeck PRIVATE -O2 -Wall -Wextra)
endif()
