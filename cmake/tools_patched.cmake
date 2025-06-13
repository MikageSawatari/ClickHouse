# Compiler

if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message (FATAL_ERROR "Compiler ${CMAKE_CXX_COMPILER_ID} is not supported. Please switch to Clang")
endif ()

# Print details to output
execute_process(COMMAND ${CMAKE_CXX_COMPILER} --version
    OUTPUT_VARIABLE COMPILER_SELF_IDENTIFICATION
    COMMAND_ERROR_IS_FATAL ANY
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message (STATUS "Using compiler:\n${COMPILER_SELF_IDENTIFICATION}")

# Require minimum compiler versions
set (CLANG_MINIMUM_VERSION 18)  # 一時的に18に変更
if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS ${CLANG_MINIMUM_VERSION})
    message (FATAL_ERROR "Compilation with Clang version ${CMAKE_CXX_COMPILER_VERSION} is unsupported, the minimum required version is ${CLANG_MINIMUM_VERSION}.")
endif ()

string (REGEX MATCHALL "[0-9]+" COMPILER_VERSION_LIST ${CMAKE_CXX_COMPILER_VERSION})
list (GET COMPILER_VERSION_LIST 0 COMPILER_VERSION_MAJOR)

# Linker
option (LINKER_NAME "Linker name or full path")

if (LINKER_NAME MATCHES "gold")
    message (FATAL_ERROR "Linking with gold is unsupported. Please use lld.")
endif ()

macro(ch_find_program var)
    if (USING_DUMMY_LAUNCHERS)
        set(${var} "${CMAKE_SOURCE_DIR}/cmake/dummy_compiler_linker.sh")
    else()
        unset(${var})
        find_program(${var} ${ARGN})
    endif()
endmacro()

if (NOT LINKER_NAME)
    if (OS_LINUX AND NOT ARCH_S390X)
        ch_find_program (LLD_PATH NAMES "ld.lld-${COMPILER_VERSION_MAJOR}" "ld.lld")
    elseif (OS_DARWIN)
        ch_find_program (LLD_PATH NAMES "ld")
        # Duplicate libraries passed to the linker is not a problem.
        set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-no_warn_duplicate_libraries")
    endif ()
    if (LLD_PATH)
        if (OS_LINUX OR OS_DARWIN)
            # Clang driver simply allows full linker path.
            set (LINKER_NAME ${LLD_PATH})
        endif ()
    endif()
endif()

if (LINKER_NAME)
    message(STATUS "Using linker: ${LINKER_NAME}")

    if (COMPILER_CLANG)
        # Clang driver requires '--ld-path=' instead of '-fuse-ld='

        # Also see: https://mropert.github.io/2024/12/07/Its_All_LTO_Me/
        if (USING_DUMMY_LAUNCHERS)
            # If dummy launchers are being used then we actually want the
            # linker to also just be the dummy launcher.
            ch_find_program(LINKER_NAME_FULL "dummy_compiler_linker.sh")
            message(STATUS "Using dummy launcher for linker: ${LINKER_NAME_FULL}")
        else()
            ch_find_program(LINKER_NAME_FULL ${LINKER_NAME})
        endif()

        if (NOT LINKER_NAME_FULL)
            message(FATAL_ERROR "Using linker ${LINKER_NAME} but can't find its full path.")
        endif()
        set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --ld-path=${LINKER_NAME_FULL}")
        set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --ld-path=${LINKER_NAME_FULL}")
    else ()
        message (FATAL_ERROR "Use of alternate linkers is only supported with Clang.")
    endif ()
else()
    if (NOT OS_FREEBSD)
        message (FATAL_ERROR "Cannot find any linker to use.")
    endif()
endif ()

# Archiver
ch_find_program (CMAKE_AR NAMES llvm-ar-${COMPILER_VERSION_MAJOR} llvm-ar HINTS ${LLVM_PREFIX}/bin)
ch_find_program (CMAKE_RANLIB NAMES llvm-ranlib-${COMPILER_VERSION_MAJOR} llvm-ranlib HINTS ${LLVM_PREFIX}/bin)

# Objcopy
ch_find_program (OBJCOPY_PATH NAMES llvm-objcopy-${COMPILER_VERSION_MAJOR} llvm-objcopy HINTS ${LLVM_PREFIX}/bin)

if (OBJCOPY_PATH)
    message (STATUS "Using objcopy: ${OBJCOPY_PATH}")
else ()
    message (STATUS "Cannot find objcopy.")
endif ()

# Strip
ch_find_program (STRIP_PATH NAMES llvm-strip-${COMPILER_VERSION_MAJOR} llvm-strip HINTS ${LLVM_PREFIX}/bin)

if (STRIP_PATH)
    message (STATUS "Using strip: ${STRIP_PATH}")
else ()
    message (STATUS "Cannot find strip.")
endif ()

if (ENABLE_CLICKHOUSE_SUDO_INSTALL)
    ch_find_program(SUDO_CMD sudo)
    message(STATUS "Using sudo: ${SUDO_CMD}")
endif()