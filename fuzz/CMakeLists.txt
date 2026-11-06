# libFuzzer targets for every decoder that meets untrusted bytes. Clang only:
#   cmake -S . -B build-fuzz -DQFL_FUZZ=ON -DCMAKE_CXX_COMPILER=clang++
#   ./build-fuzz/fuzz/fuzz_fix_decoder -max_total_time=60 fuzz/corpus/fix
set(QFL_FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -g -O1)
foreach(target fuzz_fix_decoder fuzz_itch fuzz_sbe fuzz_checkpoint)
    add_executable(${target} ${target}.cpp)
    target_link_libraries(${target} PRIVATE quantfinlib)
    target_compile_options(${target} PRIVATE ${QFL_FUZZ_FLAGS})
    target_link_options(${target} PRIVATE ${QFL_FUZZ_FLAGS})
endforeach()
