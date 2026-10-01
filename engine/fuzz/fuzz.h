#pragma once

// Every fuzz target defines LLVMFuzzerTestOneInput and fuzzSeeds(). With Clang the target links libFuzzer;
// otherwise standalone_main.cpp mutates the seeds (and random bytes) for a fixed number of runs, which runs
// as a ctest on MSVC too.

#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

// Small valid (or nearly valid) inputs that get the mutations deep into the format quickly.
std::vector<std::vector<std::uint8_t>> fuzzSeeds();
