FetchContent_Declare(
    miles
    GIT_REPOSITORY https://github.com/TheSuperHackers/miles-sdk-stub.git
    GIT_TAG        6e32700d7ba4b4713a03bf1f5ffc3b0ac8d17264
)

FetchContent_MakeAvailable(miles)

if(MSVC AND TARGET milesstub)
    # Every parent build context (Generals and GeneralsMD) shares this fetched
    # target and may populate the dependency cache. Make the object and DLL
    # deterministic here, immediately after target creation, rather than in a
    # consumer subdirectory that some configurations never enter.
    target_compile_options(milesstub PRIVATE
        "$<$<CONFIG:Release>:/Brepro>"
    )
    target_link_options(milesstub PRIVATE
        "$<$<CONFIG:Release>:/Brepro>"
        "$<$<CONFIG:Release>:/PDBALTPATH:%_PDB%>"
    )
endif()
