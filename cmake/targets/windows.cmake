# windows specific target definitions
set_target_properties(sunshine PROPERTIES LINK_SEARCH_START_STATIC 1)
set(CMAKE_FIND_LIBRARY_SUFFIXES ".dll")
find_library(ZLIB ZLIB1)
list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
        $<TARGET_OBJECTS:sunshine_rc_object>
        Windowsapp.lib
        Wtsapi32.lib)

# The setup wizard runs scripts/other-hosts.ps1 next to sunshine.exe; put it there for
# builds run from the build folder too (the installer copies it into the install folder).
file(COPY "${CMAKE_SOURCE_DIR}/src_assets/windows/misc/migration/other-hosts.ps1"
        DESTINATION "${CMAKE_BINARY_DIR}/scripts")
