add_executable(project_tempest_demo WIN32
    Code/ProjectTempestDemo.cpp
)

target_compile_features(project_tempest_demo PRIVATE cxx_std_17)
target_compile_definitions(project_tempest_demo PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(project_tempest_demo PRIVATE
    core_wwstub
    d3d8
    d3d8lib
    d3dx8
    g_wwaudio
    g_wwvegas
    gi_always
    imm32
    milesstub
    Version
    vfw32
    winmm
)

set_target_properties(project_tempest_demo PROPERTIES
    OUTPUT_NAME "ProjectTempestDemo${RTS_BUILD_OUTPUT_SUFFIX}"
)

add_custom_command(TARGET project_tempest_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Content/Art/W3D/courier.w3d"
        "$<TARGET_FILE_DIR:project_tempest_demo>/courier.w3d"
    VERBATIM
)
