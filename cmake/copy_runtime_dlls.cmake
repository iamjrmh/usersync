# Tiny helper invoked from a POST_BUILD step. Copies every path in DLLS into
# DST, but no-ops when DLLS is empty -- which is the common case for a fully
# static build. Without this guard, `cmake -E copy_if_different` with zero
# source files prints its own help text and exits 1.

if(NOT DEFINED DST)
    message(FATAL_ERROR "copy_runtime_dlls.cmake: DST must be defined")
endif()

if(DLLS)
    file(MAKE_DIRECTORY "${DST}")
    foreach(dll IN LISTS DLLS)
        if(EXISTS "${dll}")
            file(COPY "${dll}" DESTINATION "${DST}")
        endif()
    endforeach()
endif()
