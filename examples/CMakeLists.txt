
message(STATUS "Configuring SAF examples:")

# List of examples which have the common structure (<example>.h/.c, <example>_internal.h/.c) and do not require the saf_sofa_reader module:
set(example_list
    ambi_drc
    ambi_enc
    array2sh
    beamformer
    dirass
    matrixconv
    multiconv
    panner
    pitch_shifter
    powermap
    rotator
    sldoa)

# Append examples which use the saf_sofa_reader module, or print a warning
if(SAF_ENABLE_SOFA_READER_MODULE)
set(example_list ${example_list}
    ambi_bin 
    ambi_dec
    binauraliser)
else()
    message(STATUS "  Note: examples that depend on the saf_sofa_reader module have been disabled")
endif()

# Find number of examples
list(LENGTH example_list num_examples)
math(EXPR num_examples "${num_examples} - 1")

# Loop over all examples
foreach(current_example RANGE ${num_examples})
    # Get current example string
    list(GET example_list ${current_example} current_string)

    # Set-up project
    message(STATUS "  ${current_string}")
    project("${example_prefix}${current_string}" LANGUAGES C)
    add_library(${PROJECT_NAME} STATIC)

    # Link with SAF
    target_link_libraries(${PROJECT_NAME} PRIVATE saf)

    # Source files
    target_sources(${PROJECT_NAME} 
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src/${current_string}/${current_string}_internal.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/${current_string}/${current_string}_internal.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/${current_string}/${current_string}.c 
    )

    # Extra source files specific to sldoa
    if (current_string STREQUAL sldoa)
        target_sources(${PROJECT_NAME} 
        PRIVATE 
            ${CMAKE_CURRENT_SOURCE_DIR}/src/${current_string}/${current_string}_database.c
            ${CMAKE_CURRENT_SOURCE_DIR}/src/${current_string}/${current_string}_database.h
        )
    endif()

    # Include directory
    target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>   
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
endforeach()


