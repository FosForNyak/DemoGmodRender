add_library(gmdr_api STATIC
    c_api.cpp
    engine.cpp
    session.cpp
    settings.cpp
    values.cpp
)
target_link_libraries(gmdr_api PUBLIC gmdr_demo gmdr_assets gmdr_core PRIVATE gmdr_warnings)
