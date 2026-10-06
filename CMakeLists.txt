cmake_minimum_required(VERSION 3.20)
project(KeyboardMethod)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/dllmain.cpp
    src/ClassFactory.cpp
    src/TextService.cpp
    src/Compartment.cpp
    src/Register.cpp
    src/Globals.cpp
)

set(HEADERS
    include/ClassFactory.h
    include/TextService.h
    include/Compartment.h
    include/Register.h
    include/Globals.h
)

set(RESOURCE
    resources/KeyboardMethod.rc
)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS} ${RESOURCE})

target_include_directories(${PROJECT_NAME} PRIVATE include)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    SUFFIX ".dll"
    OUTPUT_NAME "KeyboardMethod"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/KeyboardMethod.def.in
    ${CMAKE_BINARY_DIR}/KeyboardMethod.def
    @ONLY
)

target_compile_options(${PROJECT_NAME} PRIVATE
    /utf-8
    /GS
    /sdl
    /guard:cf
    /Gy
)

target_link_options(${PROJECT_NAME} PRIVATE
    "/DEF:${CMAKE_BINARY_DIR}/KeyboardMethod.def"
    # Discard the import library: a real path is used instead of NUL because
    # recent MSBuild versions try to create "\\.\NUL\" and fail (MSB3191).
    "/IMPLIB:${CMAKE_BINARY_DIR}/KeyboardMethod-implib.lib"
    /DYNAMICBASE
    /NXCOMPAT
    /HIGHENTROPYVA
    /GUARD:CF
    /DEPENDENTLOADFLAG:0x800
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
)

target_link_libraries(${PROJECT_NAME} PRIVATE ole32 oleaut32 uuid shlwapi)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/icon.ico"
)

add_executable(imeinst WIN32 src/imeinst.cpp)
target_compile_definitions(imeinst PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)
target_compile_options(imeinst PRIVATE /utf-8 /GS /sdl /guard:cf /Gy)
target_link_options(imeinst PRIVATE
    /DYNAMICBASE
    /NXCOMPAT
    /HIGHENTROPYVA
    /GUARD:CF
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
)
set_target_properties(imeinst PROPERTIES
    OUTPUT_NAME "imeinst"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
target_link_libraries(imeinst PRIVATE shell32 ole32)
