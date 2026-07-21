add_executable(project_tempest_demo WIN32
    Code/ProjectTempestDemo.cpp
    Code/TempestAccessibility.cpp
    Code/TempestAccessibility.h
    Code/TempestAudio.cpp
    Code/TempestAudio.h
    Code/TempestAudioWin32.cpp
    Code/TempestInterface.cpp
    Code/TempestInterface.h
    Code/TempestPresentationD3D8.cpp
    Code/TempestPresentationD3D8.h
    Code/TempestPresentationShader.h
    Code/TempestSimulation.cpp
)

target_compile_features(project_tempest_demo PRIVATE cxx_std_20)
if(MSVC)
    # The governed package compares independently linked Release executables.
    # Remove PE timestamps and build-root-specific PDB paths.
    target_link_options(project_tempest_demo PRIVATE
        "$<$<CONFIG:Release>:/Brepro>"
        "$<$<CONFIG:Release>:/PDBALTPATH:%_PDB%>"
    )
    if(TARGET milesstub)
        target_link_options(milesstub PRIVATE
            "$<$<CONFIG:Release>:/Brepro>"
            "$<$<CONFIG:Release>:/PDBALTPATH:%_PDB%>"
        )
    endif()
endif()
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
    ole32
    Version
    vfw32
    winmm
    xaudio2
)

set_target_properties(project_tempest_demo PROPERTIES
    OUTPUT_NAME "ProjectTempestDemo${RTS_BUILD_OUTPUT_SUFFIX}"
)

set(PROJECT_TEMPEST_RUNTIME_ASSETS
    "Content/Art/W3D/courier.w3d"
    "Content/Art/W3D/courierd.w3d"
    "Content/Art/W3D/drone.w3d"
    "Content/Art/W3D/relay.w3d"
    "Content/Art/W3D/sentry.w3d"
    "Content/Art/W3D/pylon.w3d"
    "Content/Art/Textures/ptburn.tga"
    "Content/Art/Textures/ptcable.tga"
    "Content/Art/Textures/ptcyan.tga"
    "Content/Art/Textures/ptoff.tga"
    "Content/Art/Textures/ptrubber.tga"
    "Content/Art/Textures/ptsteel.tga"
    "Content/Art/Textures/ptwhite.tga"
    "Content/Art/Textures/ptmagnta.tga"
    "Content/Audio/pt_music_substation.wav"
    "Content/Audio/pt_music_pressure.wav"
    "Content/Audio/pt_music_crisis.wav"
    "Content/Audio/pt_ui_confirm.wav"
    "Content/Audio/pt_select.wav"
    "Content/Audio/pt_command.wav"
    "Content/Audio/pt_arc_pulse.wav"
    "Content/Audio/pt_alert.wav"
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

set(PROJECT_TEMPEST_ACCESSIBILITY_NOTICE_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ElectronicArtsTunableColorblindness"
)
list(APPEND PROJECT_TEMPEST_RUNTIME_ASSET_SOURCES
    "${PROJECT_TEMPEST_ACCESSIBILITY_NOTICE_ROOT}/LICENSE.txt"
    "${PROJECT_TEMPEST_ACCESSIBILITY_NOTICE_ROOT}/NOTICE.txt"
    "${PROJECT_TEMPEST_ACCESSIBILITY_NOTICE_ROOT}/SOURCE.txt"
)
list(APPEND PROJECT_TEMPEST_RUNTIME_ASSET_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_TEMPEST_ACCESSIBILITY_NOTICE_ROOT}/LICENSE.txt"
        "$<TARGET_FILE_DIR:project_tempest_demo>/EA-Tunable-Colorblindness-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_TEMPEST_ACCESSIBILITY_NOTICE_ROOT}/NOTICE.txt"
        "$<TARGET_FILE_DIR:project_tempest_demo>/EA-Tunable-Colorblindness-NOTICE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_TEMPEST_ACCESSIBILITY_NOTICE_ROOT}/SOURCE.txt"
        "$<TARGET_FILE_DIR:project_tempest_demo>/EA-Tunable-Colorblindness-SOURCE.txt"
)

add_custom_target(project_tempest_runtime_assets
    ${PROJECT_TEMPEST_RUNTIME_ASSET_COMMANDS}
    DEPENDS ${PROJECT_TEMPEST_RUNTIME_ASSET_SOURCES}
    COMMENT "Synchronising Project Tempest runtime assets"
    VERBATIM
)
add_dependencies(project_tempest_demo project_tempest_runtime_assets)

add_executable(project_tempest_sim_tests
    Code/TempestAccessibility.cpp
    Code/TempestAccessibility.h
    Code/TempestAudio.cpp
    Code/TempestAudio.h
    Code/TempestInterface.cpp
    Code/TempestInterface.h
    Code/TempestSimulation.cpp
    Tests/TempestSimulationTests.cpp
)
target_compile_features(project_tempest_sim_tests PRIVATE cxx_std_20)
target_include_directories(project_tempest_sim_tests PRIVATE Code)

add_test(NAME project_tempest_sim_tests COMMAND project_tempest_sim_tests)

add_executable(project_tempest_headless_acceptance
    Code/TempestAccessibility.cpp
    Code/TempestAccessibility.h
    Code/TempestInterface.cpp
    Code/TempestInterface.h
    Code/TempestSimulation.cpp
    Tests/TempestHeadlessAcceptance.cpp
)
target_compile_features(project_tempest_headless_acceptance PRIVATE cxx_std_20)
if(MSVC)
    target_link_options(project_tempest_headless_acceptance PRIVATE "$<$<CONFIG:Release>:/Brepro>")
endif()
target_include_directories(project_tempest_headless_acceptance PRIVATE Code)

add_test(
    NAME project_tempest_headless_acceptance
    COMMAND project_tempest_headless_acceptance
)

add_executable(project_tempest_presentation_shader_tests
    Code/TempestPresentationShader.h
    Tests/TempestPresentationShaderTests.cpp
)
target_compile_features(project_tempest_presentation_shader_tests PRIVATE cxx_std_20)
target_compile_definitions(project_tempest_presentation_shader_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
target_include_directories(project_tempest_presentation_shader_tests PRIVATE Code)
target_link_libraries(project_tempest_presentation_shader_tests PRIVATE
    advapi32
    d3d8lib
    gdi32
    user32
)
add_test(
    NAME project_tempest_presentation_shader_tests
    COMMAND project_tempest_presentation_shader_tests
)
add_custom_command(TARGET project_tempest_presentation_shader_tests POST_BUILD
    COMMAND $<TARGET_FILE:project_tempest_presentation_shader_tests>
    COMMENT "Assembling the Project Tempest accessibility shader headlessly"
    VERBATIM
)
add_dependencies(project_tempest_demo project_tempest_presentation_shader_tests)
