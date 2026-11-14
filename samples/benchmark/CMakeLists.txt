add_executable(benchmark src/Benchmark.cpp)
target_link_libraries(benchmark PRIVATE phi::app)
phi_configure_target(benchmark)
phi_target_modules(benchmark
    window_null
    rhi_vulkan
    rhi_null
    renderer_forward)
