# --- examples: 小型演示程序, 失败返回非零供 CI 冒烟 ---
foreach(example_name basic multi_thread options_tuning)
    add_executable(example_${example_name} ${example_name}.cxx)
    target_compile_features(example_${example_name} PRIVATE cxx_std_20)
    target_link_libraries(example_${example_name} PRIVATE ba7lya::snowflake)
endforeach()
