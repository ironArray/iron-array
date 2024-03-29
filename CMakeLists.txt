#
# Copyright ironArray SL 2021.
#
# All rights reserved.
#
# This software is the confidential and proprietary information of ironArray SL
# ("Confidential Information"). You shall not disclose such Confidential Information
# and shall use it only in accordance with the terms of the license agreement.
#
cmake_minimum_required (VERSION 3.12)
cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0091 NEW)

project(iarray VERSION 0.1.6)

if (MSVC)
    set(CMAKE_C_FLAGS "/W4")
else()
    set(CMAKE_C_FLAGS "-Wall -Wextra")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DMKL_ILP64 -m64")

option(MULTITHREADING "Use multithreaded iarray" OFF)
option(DISABLE_LLVM_CONFIG "Disable the use of llvm-config for finding libraries" ON)

# Disable weird MSVC warnings
if (MSVC)
    add_compile_options(/wd4204)
    add_compile_options(/wd4201)  # TODO: gix this (warning C4201: nonstandard extension used: nameless struct/union)
    add_compile_options(/wd4232)  # TODO: fix this (warning C4232: nonstandard extension used)
    add_compile_options(/wd4127)  # TODO: fix this (warning C4127: conditional expression is constant)
else()
    add_compile_options(-Wall -Wextra)
endif()

if(NOT EXISTS "${CMAKE_BINARY_DIR}/inac.cmake")
    if (NOT EXISTS "${CMAKE_SOURCE_DIR}/inac.cmake")
        message(STATUS "Downloading inac.cmake from https://github.com/inaos/inac-cmake")
        file(DOWNLOAD "https://raw.githubusercontent.com/inaos/inac-cmake/0.3/inac.cmake"
                "${CMAKE_BINARY_DIR}/inac.cmake" STATUS DS)
        if(NOT "${DS}"  MATCHES "0;")
            file(REMOVE "${CMAKE_BINARY_DIR}/inac.cmake")
            message(FATAL_ERROR "Failed to download inac.cmake")
        endif()
    else()
        message(STATUS "Use local inac.cmake")
        configure_file("${CMAKE_SOURCE_DIR}/inac.cmake" "${CMAKE_BINARY_DIR}/inac.cmake" COPYONLY)
    endif()
endif()
include("${CMAKE_BINARY_DIR}/inac.cmake")

inac_add_dependency(inac "1.0.22")
inac_add_dependency(hwloc "2.3.2")

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

set(CATERVA_BUILD_TESTS OFF)
set(CATERVA_BUILD_EXAMPLES OFF)
add_subdirectory(contribs/caterva)
include_directories(contribs/caterva/caterva)
include_directories(contribs/caterva/contribs/c-blosc2/blosc)
include_directories(contribs/caterva/contribs/c-blosc2/include)

add_subdirectory(contribs/minjugg)
include_directories(contribs/minjugg/include)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/contribs/c-blosc2/cmake)
#configure Intel MKL
find_package(MKL)
#configure Intel IPP
find_package(IPP)
# configure OMP
if (MULTITHREADING)
    find_package(OMP)
endif()
find_package(SVML)
# For some reason, this is needed for building wheels (TODO: check this out again after wheels are building)
if (UNIX)
	set(INAC_DEPENDENCY_LIBS ${INAC_DEPENDENCY_LIBS} -lz)
endif()

# Find the libraries that correspond to the LLVM components
# that we wish to use
llvm_map_components_to_libnames(llvm_libs support core irreader executionengine
   bitwriter passes vectorize mcjit asmprinter x86info x86codegen)
if (UNIX AND NOT APPLE AND NOT DISABLE_LLVM_CONFIG)
    # The next should be the canonical way, but for now only Linux and some llvm distros seems safe
    execute_process(COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --libs OUTPUT_VARIABLE llvm_libs
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
endif(UNIX AND NOT APPLE AND NOT DISABLE_LLVM_CONFIG)
set(INAC_DEPENDENCY_LIBS ${INAC_DEPENDENCY_LIBS} ${IPP_LIBRARIES} ${llvm_libs})

set(SRC ${CMAKE_SOURCE_DIR}/src)
file(GLOB src
        ${SRC_DIR}/*.c
        ${SRC_DIR}/linalg/*.c
        ${SRC_DIR}/reductions/*.c
        ${SRC_DIR}/reductions/operations/*.c
        ${SRC_DIR}/constructors/*.c
        ${SRC_DIR}/constructors/random/*.c
        ${SRC_DIR}/linalg/matmul/*.c
        ${SRC_DIR}/btune/*.c
        ${SRC_DIR}/indexing/*.c)

add_library(iarray_c ${src})
set_target_properties(
        iarray_c
        PROPERTIES
        COMPILE_DEFINITIONS INA_LIB=1)

include_directories("${PROJECT_BINARY_DIR}" "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/inac"
        "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src")

if (ENABLE_ASAN)
    message(STATUS "Enable sanitizers")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Os -fno-omit-frame-pointer -fsanitize=address")
    link_libraries("-g -fsanitize=address")
endif()

if (DO_COVERAGE)
    target_compile_options(iarray_c PRIVATE -fprofile-arcs -ftest-coverage)
    inac_merge_static_libs(iarrays iarray_c blosc2_static caterva_static ${INAC_LIBS})
    target_link_libraries(iarrays -fprofile-arcs)
else()
    inac_merge_static_libs(iarrays iarray_c blosc2_static caterva_static ${INAC_LIBS})
endif()

if (UNIX)
    if (NOT APPLE)
		set(CMAKE_EXE_LINKER_FLAGS    "-Wl,--no-as-needed ${CMAKE_EXE_LINKER_FLAGS}")
		set(CMAKE_SHARED_LINKER_FLAGS "-Wl,--no-as-needed ${CMAKE_SHARED_LINKER_FLAGS}")
	endif()
	set(PLATFORM_LIBS ${PLATFORM_LIBS} pthread)
	set(INAC_DEPENDENCY_LIBS minjugg minjuggutil ${INAC_DEPENDENCY_LIBS} blosc2_static ${IPP_LIBRARIES})
else(UNIX)
	set(INAC_DEPENDENCY_LIBS minjugg minjuggutil ${INAC_DEPENDENCY_LIBS})
endif(UNIX)

inac_add_tests(iarrays)

if (DO_COVERAGE)
    include(CTest)
    enable_testing()
    add_test(NAME iarray_tests COMMAND tests)
    set_tests_properties(iarray_tests PROPERTIES LABELS "iarray")
endif()
#inac_add_benchmarks(iarrays)
inac_add_tools(iarrays)
inac_add_examples(iarrays)

inac_enable_trace(${CMAKE_BUILD_TYPE} 3)

# Playing with OpenMP (available mainly on GCC)
#if (UNIX AND NOT CMAKE_C_COMPILER_ID STREQUAL Clang)
#    set_property(
#            TARGET vectors
#            APPEND PROPERTY LINK_FLAGS "-fopenmp")
#endif ()

#if (MSVC)
#    install(TARGETS iarray
#            DESTINATION libs
#            COMPONENT libraries)
#else()
#    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libiarray.a
#            DESTINATION lib
#            COMPONENT libraries)
#endif()

# dist lib
add_definitions(-DINA_DLL)
add_definitions(-DINA_LIB)
add_library(iarray SHARED ${src})
target_link_libraries(iarray ${INAC_LIBS} blosc2_static caterva_static minjugg minjuggutil ${INAC_DEPENDENCY_LIBS} ${PLATFORM_LIBS})

install(TARGETS iarray
        DESTINATION lib
        COMPONENT libraries)
install(FILES ${INAC_DEPENDENCY_BINS}
            DESTINATION lib
            COMPONENT libraries)

inac_package()

# Copy test files
file(GLOB TESTS_DATA tests/data/*.iarray)

foreach (data ${TESTS_DATA})
    file(COPY ${data}
            DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/)
endforeach(data)
