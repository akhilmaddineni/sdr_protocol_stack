include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(unit_tests
    unit/test_ring_buffer.cpp
    unit/test_ring_buffer_stress.cpp
    unit/test_dsp.cpp
    unit/test_dsp_simd.cpp
    unit/test_plugin_loader.cpp
    unit/test_thread_shutdown.cpp
    ../src/buffer/SpscRingBuffer.cpp
    ../src/dsp/FirFilter.cpp
    ../src/dsp/FirFilterSimd.cpp
    ../src/core/PluginManager.cpp
    ../src/ingress/FileSource.cpp
    ../src/core/Orchestrator.cpp
)

target_include_directories(unit_tests PRIVATE ../include ../src)
target_link_libraries(unit_tests PRIVATE GTest::gtest_main Threads::Threads dl)

add_executable(integration_tests
    integration/test_file_to_decoder.cpp
    integration/test_pipeline_end_to_end.cpp
    ../src/ingress/FileSource.cpp
    ../src/buffer/SpscRingBuffer.cpp
    ../src/dsp/FirFilter.cpp
    ../src/core/Orchestrator.cpp
    ../src/core/PluginManager.cpp
)

target_include_directories(integration_tests PRIVATE ../include ../src)
target_link_libraries(integration_tests PRIVATE GTest::gtest_main Threads::Threads dl)

include(GoogleTest)
gtest_discover_tests(unit_tests)
gtest_discover_tests(integration_tests)
