add_executable(project_tempest_demo WIN32
    Code/ProjectTempestDemo.cpp
    Code/TempestSimulation.cpp
)

target_compile_features(project_tempest_demo PRIVATE cxx_std_20)
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

set(PROJECT_TEMPEST_RUNTIME_ASSETS
    "Content/Art/W3D/courier.w3d"
    "Content/Art/W3D/courierd.w3d"
    "Content/Art/W3D/drone.w3d"
    "Content/Art/W3D/relay.w3d"
    "Content/Art/Textures/ptburn.tga"
    "Content/Art/Textures/ptcable.tga"
    "Content/Art/Textures/ptcyan.tga"
    "Content/Art/Textures/ptoff.tga"
    "Content/Art/Textures/ptrubber.tga"
    "Content/Art/Textures/ptsteel.tga"
    "Content/Art/Textures/ptwhite.tga"
    "Content/Art/Textures/ptmagnta.tga"
)

set(PROJECT_TEMPEST_RUNTIME_ASSET_SOURCES)
set(PROJECT_TEMPEST_RUNTIME_ASSET_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:project_tempest_demo>"
)
foreach(asset IN LISTS PROJECT_TEMPEST_RUNTIME_ASSETS)
    list(APPEND PROJECT_TEMPEST_RUNTIME_ASSET_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${asset}")
    list(APPEND PROJECT_TEMPEST_RUNTIME_ASSET_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/${asset}"
            "$<TARGET_FILE_DIR:project_tempest_demo>"
    )
endforeach()

add_custom_target(project_tempest_runtime_assets
    ${PROJECT_TEMPEST_RUNTIME_ASSET_COMMANDS}
    DEPENDS ${PROJECT_TEMPEST_RUNTIME_ASSET_SOURCES}
    COMMENT "Synchronising Project Tempest runtime assets"
    VERBATIM
)
add_dependencies(project_tempest_demo project_tempest_runtime_assets)

add_executable(project_tempest_sim_tests
    Code/TempestSimulation.cpp
    Tests/TempestSimulationTests.cpp
)
target_compile_features(project_tempest_sim_tests PRIVATE cxx_std_20)
target_include_directories(project_tempest_sim_tests PRIVATE Code)

add_test(NAME project_tempest_sim_tests COMMAND project_tempest_sim_tests)
