cmake_minimum_required(VERSION 3.21)
project(usersync LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# NOTE: do NOT use add_compile_options() at directory scope here -- those
# flags leak into every FetchContent dependency added below, and `/MP`
# in particular gets forwarded by nvcc to cl.exe and breaks the CUDA
# build with "nvcc fatal: A single input file is required for a non-link
# phase when an outputfile is specified". Apply our flags per-target
# at the bottom of this file instead.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Force every fetched dep to build static. whisper.cpp defaults BUILD_SHARED_LIBS
# to ON on Windows, which otherwise leaves whisper.dll / ggml.dll / glfw3.dll
# next to the exe.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# whisper.cpp — speech recognition with word-level timestamps
# ---------------------------------------------------------------------------
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
# Pick the fastest backend available; users can override at configure time.
option(USERSYNC_WHISPER_CUDA   "Enable CUDA backend for whisper"   OFF)
option(USERSYNC_WHISPER_VULKAN "Enable Vulkan backend for whisper" OFF)
option(USERSYNC_WHISPER_METAL  "Enable Metal backend (macOS)"      OFF)
if(USERSYNC_WHISPER_CUDA)
    set(GGML_CUDA   ON CACHE BOOL "" FORCE)
endif()
if(USERSYNC_WHISPER_VULKAN)
    set(GGML_VULKAN ON CACHE BOOL "" FORCE)
endif()
if(USERSYNC_WHISPER_METAL)
    set(GGML_METAL  ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    whisper
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
    GIT_TAG        v1.7.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(whisper)

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
# miniaudio — single-header decoder for mp3 / wav / flac / ogg
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
# usersync executable
# ---------------------------------------------------------------------------
set(USERSYNC_SOURCES
    src/main.cpp
    src/App.cpp
    src/AudioLoader.cpp
    src/AudioPlayer.cpp
    src/Transcriber.cpp
    src/Aligner.cpp
    src/ForcedAligner.cpp
    src/IterativeAligner.cpp
    src/LrcWriter.cpp
    src/FileDialog.cpp
    src/GpuInfo.cpp
    src/ModelDownloader.cpp
    src/WhisperXRunner.cpp
    src/SplitRunner.cpp
    src/Gui.cpp
)
if(WIN32)
    # Embed JURMRWEED.ico as the exe icon resource. rc.exe is auto-detected
    # by CMake on Windows.
    list(APPEND USERSYNC_SOURCES ${CMAKE_SOURCE_DIR}/usersync.rc)
endif()

add_executable(usersync ${USERSYNC_SOURCES})

# Per-target compile flags. Keeping these off the directory scope means
# whisper.cpp / glfw / imgui don't inherit them. Critically, /MP is NOT
# passed to whisper because nvcc forwards it to cl.exe which then fails.
if(MSVC)
    target_compile_options(usersync PRIVATE
        /W4 /permissive- /Zc:__cplusplus /utf-8 /MP)
    target_compile_definitions(usersync PRIVATE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    # imgui and miniaudio benefit from /MP for incremental builds too, but
    # they're pure C/C++ so it's safe.
    target_compile_options(imgui      PRIVATE /MP /utf-8)
    target_compile_options(miniaudio  PRIVATE /MP)
    target_compile_definitions(imgui     PRIVATE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(miniaudio PRIVATE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(usersync PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Propagate the selected GPU backend into the C++ code so the runtime
# "Compiled backend" tooltip reports accurately. whisper.cpp's
# whisper_print_system_info() doesn't include GPU backend flags in v1.7.x,
# so we have to track them ourselves at build time.
if(USERSYNC_WHISPER_CUDA)
    target_compile_definitions(usersync PRIVATE USERSYNC_BUILT_WITH_CUDA=1)
endif()
if(USERSYNC_WHISPER_VULKAN)
    target_compile_definitions(usersync PRIVATE USERSYNC_BUILT_WITH_VULKAN=1)
endif()
if(USERSYNC_WHISPER_METAL)
    target_compile_definitions(usersync PRIVATE USERSYNC_BUILT_WITH_METAL=1)
endif()
target_include_directories(usersync PRIVATE src)
target_link_libraries(usersync PRIVATE
    whisper
    imgui
    miniaudio
    glfw
)
if(WIN32)
    target_link_libraries(usersync PRIVATE opengl32 comdlg32 dxgi winhttp shell32)
endif()

set_target_properties(usersync PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    WIN32_EXECUTABLE         TRUE   # /SUBSYSTEM:WINDOWS -- no console window
)
if(MSVC)
    # WIN32_EXECUTABLE defaults to expecting WinMain on MSVC; force the linker
    # to use the standard main() we already have.
    target_link_options(usersync PRIVATE /ENTRY:mainCRTStartup)
endif()

# Belt-and-suspenders: if any dep ends up shared anyway (e.g. CUDA / Vulkan
# backends drag in runtime DLLs), drop them next to usersync.exe after build.
# Routed through a tiny script so the step no-ops when the DLL list is empty
# (which is the common static-only case).
if(WIN32)
    add_custom_command(TARGET usersync POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DDLLS=$<TARGET_RUNTIME_DLLS:usersync>"
            "-DDST=$<TARGET_FILE_DIR:usersync>"
            -P "${CMAKE_SOURCE_DIR}/cmake/copy_runtime_dlls.cmake"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endif()
