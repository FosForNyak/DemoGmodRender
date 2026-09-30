# Fuzz targets for every parser of untrusted data (spec §11). With Clang and GMDR_BUILD_FUZZ: libFuzzer
# executables (run in CI, 60 s each). Otherwise: the same targets with a deterministic mutation driver,
# registered as ctest smoke tests so they also run on MSVC with checked iterators.
set(GMDR_FUZZ_TARGETS demo tables archives statedb)
set(GMDR_LIBFUZZER OFF)
if(GMDR_BUILD_FUZZ AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
    set(GMDR_LIBFUZZER ON)
endif()

foreach(target ${GMDR_FUZZ_TARGETS})
    if(GMDR_LIBFUZZER)
        add_executable(fuzz_${target} fuzz_${target}.cpp)
        target_compile_options(fuzz_${target} PRIVATE -fsanitize=fuzzer)
        target_link_options(fuzz_${target} PRIVATE -fsanitize=fuzzer)
    else()
        add_executable(fuzz_${target} fuzz_${target}.cpp standalone_main.cpp)
        add_test(NAME fuzz_smoke_${target} COMMAND fuzz_${target} --runs=3000)
    endif()
    target_link_libraries(fuzz_${target} PRIVATE gmdr_warnings gmdr_demo gmdr_assets)
endforeach()
