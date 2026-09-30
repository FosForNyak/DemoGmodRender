add_library(gmdr_assets STATIC
    archives.cpp
    content.cpp
    locator.cpp
    vdf.cpp
    vfs.cpp
)
target_link_libraries(gmdr_assets PUBLIC gmdr_core PRIVATE gmdr_warnings)
if(WIN32)
    target_link_libraries(gmdr_assets PRIVATE advapi32)
endif()
