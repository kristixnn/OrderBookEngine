include(FetchContent)

# 1. Automatically fetch and build GoogleTest from GitHub
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
# Force MSVC to use the correct runtime library matching your project settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# 2. Define the test executable
# We pull in Orderbook.cpp from the parent directory so the test binary has the engine logic
add_executable(OrderbookTests
    Test.cpp
    ../src/Orderbook.cpp
)
# Add this line right under add_executable(OrderbookTests ...)
target_compile_definitions(OrderbookTests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# 3. Tell the test binary where to find your engine headers (Orderbook.h, Usings.h, etc.)
target_include_directories(OrderbookTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

# 4. Link GoogleTest (GTest::gtest_main provides the main() entry point automatically)
target_link_libraries(OrderbookTests
    PRIVATE
        GTest::gtest_main
)

# 5. Enable automatic test discovery for Visual Studio / CTest
include(GoogleTest)
gtest_discover_tests(OrderbookTests)