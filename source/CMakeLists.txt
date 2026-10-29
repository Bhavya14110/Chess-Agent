cmake_minimum_required(VERSION 3.20)
project(corvus CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# NATIVE=ON: tuned for the hosting machine (AVX2/BMI2). OFF: portable x86-64 + popcnt only.
option(NATIVE "build with -march=native" ON)
option(PEXT "use BMI2 pext for sliding attacks (only if the cpu does it fast)" ON)

file(GLOB SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(corvus ${SRC})

target_compile_options(corvus PRIVATE -Wall -Wextra -Wno-unused-parameter)
# release never aborts on asserts
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

if(NATIVE)
  target_compile_options(corvus PRIVATE -march=native)
  if(PEXT)
    target_compile_definitions(corvus PRIVATE USE_PEXT)
  endif()
else()
  target_compile_options(corvus PRIVATE -march=x86-64 -mpopcnt -mtune=generic)
endif()

find_package(Threads REQUIRED)
target_link_libraries(corvus PRIVATE Threads::Threads)
if(WIN32)
  # one self-contained exe, no mingw dlls needed next to it
  target_link_options(corvus PRIVATE -static -static-libgcc -static-libstdc++)
endif()
