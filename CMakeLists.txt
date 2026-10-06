cmake_minimum_required(VERSION 3.21)

project(umod VERSION 0.0.6 LANGUAGES CXX RC)

if(NOT MSVC)
    message(FATAL_ERROR "umod.wcx currently builds only with MSVC")
endif()

# Link the CRT statically so the plugin has no runtime DLL dependencies.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(umod SHARED
    umod.cpp
    umod.def
    umod.rc
)

target_compile_definitions(umod PRIVATE
    WIN32
    _WINDOWS
    _USRDLL
    UMOD_EXPORTS
)

# Total Commander looks for .wcx (32-bit) and .wcx64 (64-bit) plugins.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(UMOD_SUFFIX ".wcx64")
    target_compile_definitions(umod PRIVATE UMOD_WIN64)
else()
    set(UMOD_SUFFIX ".wcx")
endif()

set_target_properties(umod PROPERTIES
    PREFIX ""
    SUFFIX "${UMOD_SUFFIX}"
)
