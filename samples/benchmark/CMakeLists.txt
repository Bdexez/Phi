add_executable(benchmark src/Benchmark.cpp)
target_link_libraries(benchmark PRIVATE bdex::app)
bdex_configure_target(benchmark)
bdex_target_modules(benchmark
    window_null
    rhi_vulkan
    rhi_null
    renderer_forward)
