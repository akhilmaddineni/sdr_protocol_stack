add_executable(sdr_main
    main.cpp
    ingress/FileSource.cpp
    buffer/SpscRingBuffer.cpp
    dsp/FirFilter.cpp
    core/Orchestrator.cpp
    core/PluginManager.cpp
)

target_link_libraries(sdr_main PRIVATE Threads::Threads dl)
target_include_directories(sdr_main PRIVATE ../include)

# Enable C++20
set_target_properties(sdr_main PROPERTIES CXX_STANDARD 20)
