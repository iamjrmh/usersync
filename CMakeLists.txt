cmake_minimum_required(VERSION 3.21)
project(usersync-forked LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Force every fetched dep to build static so nothing extra needs to sit
# next to the exe.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# GLFW — windowing for the ImGui frontend
# ---------------------------------------------------------------------------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw)

# ---------------------------------------------------------------------------
# Dear ImGui — vendored as a static lib (upstream ships no CMakeLists)
# ---------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)
if(WIN32)
    target_link_libraries(imgui PUBLIC opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(imgui PUBLIC OpenGL::GL)
endif()

# ---------------------------------------------------------------------------
# miniaudio — single-header decoder/player for mp3 / wav / flac / ogg
# ---------------------------------------------------------------------------
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(miniaudio)

add_library(miniaudio STATIC src/miniaudio_impl.c)
target_include_directories(miniaudio PUBLIC ${miniaudio_SOURCE_DIR})
if(UNIX AND NOT APPLE)
    target_link_libraries(miniaudio PUBLIC m dl pthread)
endif()

# ---------------------------------------------------------------------------
# usersync-forked executable
# ---------------------------------------------------------------------------
set(USERSYNC_FORKED_SOURCES
    src/main.cpp
    src/App.cpp
    src/AudioPlayer.cpp
    src/LrcWriter.cpp
    src/FileDialog.cpp
    src/Gui.cpp
)
if(WIN32)
    # Embed JURMRWEED.ico as the exe icon resource. rc.exe is auto-detected
    # by CMake on Windows.
    list(APPEND USERSYNC_FORKED_SOURCES ${CMAKE_SOURCE_DIR}/usersync-forked.rc)
endif()

add_executable(usersync-forked ${USERSYNC_FORKED_SOURCES})

if(MSVC)
    target_compile_options(usersync-forked PRIVATE
        /W4 /permissive- /Zc:__cplusplus /utf-8 /MP)
    target_compile_definitions(usersync-forked PRIVATE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(imgui      PRIVATE /MP /utf-8)
    target_compile_options(miniaudio  PRIVATE /MP)
    target_compile_definitions(imgui     PRIVATE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(miniaudio PRIVATE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(usersync-forked PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

target_include_directories(usersync-forked PRIVATE src)
target_link_libraries(usersync-forked PRIVATE
    imgui
    miniaudio
    glfw
)
if(WIN32)
    target_link_libraries(usersync-forked PRIVATE opengl32 comdlg32 shell32)
endif()

set_target_properties(usersync-forked PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    WIN32_EXECUTABLE         TRUE   # /SUBSYSTEM:WINDOWS -- no console window
)
if(MSVC)
    # WIN32_EXECUTABLE defaults to expecting WinMain on MSVC; force the linker
    # to use the standard main() we already have.
    target_link_options(usersync-forked PRIVATE /ENTRY:mainCRTStartup)
endif()

# Belt-and-suspenders: drop any runtime DLLs a dep ends up needing next to
# the exe after build. No-ops in the common static-only case.
if(WIN32)
    add_custom_command(TARGET usersync-forked POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DDLLS=$<TARGET_RUNTIME_DLLS:usersync-forked>"
            "-DDST=$<TARGET_FILE_DIR:usersync-forked>"
            -P "${CMAKE_SOURCE_DIR}/cmake/copy_runtime_dlls.cmake"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endif()
