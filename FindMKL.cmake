# Find the Intel MKL (Math Kernel Library)
#
# MKL_FOUND - System has MKL
# MKL_INCLUDE_DIRS - MKL include files directories
# MKL_LIBRARIES - The MKL libraries
#
# The environment variable MKLROOT is used to find the installation location.
# If the environment variable is not set we'll look for it in the default installation locations.
#
# Usage:
#
# find_package(MKL)
# if(MKL_FOUND)
#     target_link_libraries(TARGET ${MKL_LIBRARIES})
# endif()

# Currently we take a couple of assumptions:
#
# 1. We only use the sequential version of the MKL
# 2. We only use 64bit
#

find_path(MKL_ROOT_DIR
    include/mkl.h
    PATHS
        $ENV{MKLROOT}
        /opt/intel/compilers_and_libraries/linux/mkl
        /opt/intel/compilers_and_libraries/mac/mkl
        "C:/IntelSWTools/compilers_and_libraries/windows/mkl/"
        "C:/Program Files (x86)/IntelSWTools/compilers_and_libraries/windows/mkl"
        $ENV{HOME}/miniconda3
        $ENV{CONDA_PREFIX}
        $ENV{HOME}/miniconda3/envs/iarray
        $ENV{USERPROFILE}/miniconda3/Library
        "C:/Miniconda37-x64/Library" # Making AppVeyor happy
        $ENV{CONDA}/envs/iArrayEnv # Azure pipelines
        /Users/vsts/.conda/envs/iArrayEnv # Azure pipelines
        C:/Miniconda/envs/iArrayEnv/Library # Azure pipelines
)

find_path(MKL_INCLUDE_DIR
        mkl.h
        PATHS
        ${MKL_ROOT_DIR}/include
        )


if(WIN32)
    set(MKL_SEARCH_LIB mkl_core.lib)
    if(MULTITHREADING)
        message("MKL Multithreading mode")
        set(MKL_LIBS mkl_intel_lp64.lib mkl_core.lib mkl_intel_thread.lib)
    else()
        message("MKL Sequential mode")
        set(MKL_LIBS mkl_intel_lp64.lib mkl_core.lib mkl_sequential.lib)
    endif()
elseif(APPLE)
    set(MKL_SEARCH_LIB libmkl_core.a)
    if(MULTITHREADING)
        message("MKL Multithreading mode")
        set(MKL_LIBS libmkl_intel_ilp64.a libmkl_core.a libmkl_intel_thread.a)
    else()
        message("MKL Sequential mode")
        set(MKL_LIBS libmkl_intel_ilp64.a libmkl_core.a libmkl_sequential.a)
    endif()
else() # Linux
    set(MKL_SEARCH_LIB libmkl_core.a)
    if(MULTITHREADING)
        message("MKL Multithreading mode")
        set(MKL_LIBS libmkl_intel_lp64.a libmkl_intel_thread.a libmkl_core.a libmkl_intel_thread.a)
    else()
        message("MKL Sequential mode")
        set(MKL_LIBS libmkl_intel_lp64.a libmkl_sequential.a libmkl_core.a libmkl_sequential.a)
    endif()
endif()


find_path(MKL_LIB_SEARCHPATH
    ${MKL_SEARCH_LIB}
    PATHS
        ${MKL_ROOT_DIR}/lib/intel64
        ${MKL_ROOT_DIR}/lib
)

foreach (LIB ${MKL_LIBS})
    find_library(${LIB}_PATH ${LIB} PATHS ${MKL_LIB_SEARCHPATH})
    if(${LIB}_PATH)
        set(MKL_LIBRARIES ${MKL_LIBRARIES} ${${LIB}_PATH})
        message(STATUS "Found MKL ${LIB} in: ${${LIB}_PATH}")
    else()
        message(STATUS "Could not find ${LIB}: disabling MKL")
    endif()
endforeach()

set(MKL_INCLUDE_DIRS ${MKL_INCLUDE_DIR})

include_directories(${MKL_INCLUDE_DIRS})
set(INAC_DEPENDENCY_LIBS ${INAC_DEPENDENCY_LIBS} ${MKL_LIBRARIES})
