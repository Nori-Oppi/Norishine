cmake_minimum_required(VERSION 3.20)

project(sunshine_tools)

include_directories(${CMAKE_SOURCE_DIR})

add_executable(dxgi-info dxgi.cpp)
set_target_properties(dxgi-info PROPERTIES CXX_STANDARD 23)
target_link_libraries(dxgi-info
        ${CMAKE_THREAD_LIBS_INIT}
        dxgi
        ${PLATFORM_LIBRARIES})
target_compile_options(dxgi-info PRIVATE ${SUNSHINE_COMPILE_OPTIONS})

add_executable(audio-info audio.cpp utils.cpp)
set_target_properties(audio-info PROPERTIES CXX_STANDARD 23)
target_link_libraries(audio-info
        ${Boost_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ksuser
        ${PLATFORM_LIBRARIES})
target_compile_options(audio-info PRIVATE ${SUNSHINE_COMPILE_OPTIONS})

add_executable(sunshinesvc sunshinesvc.cpp)
set_target_properties(sunshinesvc PROPERTIES CXX_STANDARD 23)
target_link_libraries(sunshinesvc
        ${CMAKE_THREAD_LIBS_INIT}
        wtsapi32
        ${PLATFORM_LIBRARIES})
target_compile_options(sunshinesvc PRIVATE ${SUNSHINE_COMPILE_OPTIONS})

# Moving test picture for the optimisation test streams (N6)
add_executable(norishine-testpattern WIN32 testpattern.cpp)
set_target_properties(norishine-testpattern PROPERTIES CXX_STANDARD 23)
target_compile_definitions(norishine-testpattern PRIVATE UNICODE _UNICODE)
target_link_options(norishine-testpattern PRIVATE -municode)
target_link_libraries(norishine-testpattern
        dwmapi
        shell32
        ${PLATFORM_LIBRARIES})
target_compile_options(norishine-testpattern PRIVATE ${SUNSHINE_COMPILE_OPTIONS})
