add_library(gmdr_demo STATIC
    commands.cpp
    game_events.cpp
    header.cpp
    import.cpp
    lzss.cpp
    parser.cpp
    props.cpp
    protocol.cpp
    send_tables.cpp
    string_tables.cpp
    statedb/codec.cpp
    statedb/reader.cpp
    statedb/writer.cpp
)
target_link_libraries(gmdr_demo PUBLIC gmdr_core PRIVATE gmdr_warnings)
