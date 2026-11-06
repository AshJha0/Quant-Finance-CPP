# The IEX replay tool. Reads a decompressed capture from a file or stdin; data
# files stay outside the repository. See docs/REPLAY_VALIDATION.md.
add_executable(iex_replay iex_replay_main.cpp)
target_link_libraries(iex_replay PRIVATE quantfinlib)

if(QFL_FUZZ)
    set(QFL_FUZZ_FLAGS_IEX -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -g -O1)
    add_executable(fuzz_iex fuzz_iex.cpp)
    target_link_libraries(fuzz_iex PRIVATE quantfinlib)
    target_compile_options(fuzz_iex PRIVATE ${QFL_FUZZ_FLAGS_IEX})
    target_link_options(fuzz_iex PRIVATE ${QFL_FUZZ_FLAGS_IEX})
endif()
