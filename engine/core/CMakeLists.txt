add_library(gmdr_core STATIC
    bit_reader.cpp
    compress.cpp
    file.cpp
    hash.cpp
    log.cpp
    paths.cpp
    process.cpp
    text.cpp
)
target_include_directories(gmdr_core PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(gmdr_core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE gmdr_warnings ${GMDR_ZSTD} BLAKE3::blake3 xxHash::xxhash)
