cmake_minimum_required(VERSION 3.14)
project(espresso_machine_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- GoogleTest via FetchContent ------------------------------------------
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---- Include paths ---------------------------------------------------------
# mock/  → resolves  esphome/core/*.h  with test stubs
# ../..  → resolves  espresso_machine/*, espresso_machine_valve/*, etc.
include_directories(
  mock
  ../..
)

# ---- Component sources compiled with mock HAL ------------------------------
set(COMPONENT_SOURCES
  ../../components/espresso_machine/espresso_machine.cpp
  ../../components/espresso_machine_valve/valve.cpp
  ../../components/espresso_machine_pump/pump.cpp
  ../../components/espresso_machine_grinder/grinder.cpp
  ../../components/espresso_machine_flow_meter/flow_meter.cpp
)

# ---- Global mock state (shared across all translation units) ---------------
add_library(mock_hal OBJECT mock_hal.cpp)
target_include_directories(mock_hal PUBLIC mock)

# ---- Test executable -------------------------------------------------------
add_executable(all_tests
  $<TARGET_OBJECTS:mock_hal>
  ${COMPONENT_SOURCES}
  test_valve.cpp
  test_pump.cpp
  test_grinder.cpp
  test_flow_meter.cpp
  test_orchestrator.cpp
)

target_include_directories(all_tests PRIVATE mock ../.. ../../components)
target_link_libraries(all_tests GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(all_tests)
