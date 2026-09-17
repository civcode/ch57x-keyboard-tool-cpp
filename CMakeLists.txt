cmake_minimum_required(VERSION 3.14)
project(ch57x-keyboard-tool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# --- libusb-1.0 (the C equivalent of the Rust `rusb` crate) -----------------
# Prefer pkg-config (gives correct include dirs + library name), fall back to
# plain find_path/find_library.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
  set(LIBUSB_INC ${LIBUSB_INCLUDE_DIRS})
  set(LIBUSB_LIB ${LIBUSB_LIBRARIES})
else()
  find_path(LIBUSB_INC NAMES libusb-1.0/libusb.h)
  find_library(LIBUSB_LIB NAMES usb-1.0)
  if(NOT LIBUSB_INC OR NOT LIBUSB_LIB)
    message(FATAL_ERROR
      "libusb-1.0 not found.\n"
      "  Debian/Ubuntu: sudo apt install libusb-1.0-dev\n"
      "  Fedora:        sudo dnf install libusb1-devel\n"
      "  macOS:         brew install libusb")
  endif()
endif()

# Common object library shared by both executables.
add_library(ch57x-core STATIC
  src/codes.cpp
  src/parser.cpp
  src/config.cpp
  src/yaml.cpp
  src/keyboard.cpp
  src/capture.cpp
  src/usb.cpp
)
target_include_directories(ch57x-core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${LIBUSB_INC}
)
target_link_libraries(ch57x-core PUBLIC ${LIBUSB_LIB})

add_executable(ch57x-keyboard-tool src/main.cpp)
target_link_libraries(ch57x-keyboard-tool PRIVATE ch57x-core)

# A tiny self-test (parser + config + orientation + byte encoding); needs no USB.
add_executable(ch57x-selftest src/selftest.cpp)
target_link_libraries(ch57x-selftest PRIVATE ch57x-core)

enable_testing()
add_test(NAME selftest COMMAND ch57x-selftest)

# Golden dry-run dump: the exact bytes `upload config.yaml` would transmit.
# Runs from the source dir so the config path in the header matches the golden file.
add_test(NAME dump-golden
  COMMAND sh -c "$<TARGET_FILE:ch57x-keyboard-tool> dump config.yaml | diff -u expected_dump.txt -")
set_tests_properties(dump-golden PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
