add_library(gmdr_demo STATIC
    commands.cpp
    game_events.cpp
    header.cpp
    lzss.cpp
    parser.cpp
    props.cpp
    protocol.cpp
    send_tables.cpp
    string_tables.cpp
)
target_link_libraries(gmdr_demo PUBLIC gmdr_core PRIVATE gmdr_warnings)
