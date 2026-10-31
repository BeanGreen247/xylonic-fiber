cmake_minimum_required(VERSION 3.20)
project(xylonic_fiber_android LANGUAGES C CXX)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/SDL2 SDL2)
# The app project lives two levels up from here; its CMakeLists provides the `main` shared library.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../.. xylonic-fiber)
