cmake_minimum_required(VERSION 3.20)

# Set the project name
project(CoreCLR)

include(../../eng/native/configurepaths.cmake)
include(${CLR_ENG_NATIVE_DIR}/configurecompiler.cmake)

include_directories("${CLR_SRC_NATIVE_DIR}")
include_directories("${CLR_SRC_NATIVE_DIR}/inc")

if(MSVC)
  set(CMAKE_CXX_STANDARD_LIBRARIES "") # do not link against standard win32 libs i.e. kernel32, uuid, user32, etc.
endif (MSVC)

# Set commonly used directory names
set(CLR_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(VM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vm)
set(GENERATED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/inc)
set(GENERATED_EVENTING_DIR ${CMAKE_CURRENT_BINARY_DIR}/Eventing)
set(PAL_REDEFINES_FILE ${CMAKE_CURRENT_SOURCE_DIR}/dlls/mscordac/palredefines.S)

# Avoid logging when skipping up-to-date copies
set(CMAKE_INSTALL_MESSAGE LAZY)

set(CORECLR_SET_RPATH ON)
if(CORECLR_SET_RPATH)
    # Enable @rpath support for shared libraries.
    set(MACOSX_RPATH ON)
endif(CORECLR_SET_RPATH)

if(CLR_CMAKE_HOST_MACCATALYST OR CLR_CMAKE_HOST_IOS OR CLR_CMAKE_HOST_TVOS OR CLR_CMAKE_HOST_BROWSER OR CLR_CMAKE_HOST_ANDROID)
  set(FEATURE_STANDALONE_GC 0)
endif()

OPTION(CLR_CMAKE_ENABLE_CODE_COVERAGE "Enable code coverage" OFF)

if (DEFINED CLR_CMAKE_ICU_DIR)
  include_directories(${CLR_CMAKE_ICU_DIR}/include)
endif(DEFINED CLR_CMAKE_ICU_DIR)

if (CLR_CMAKE_TARGET_ARCH_WASM)
    add_compile_options(-fwasm-exceptions)
endif()

#----------------------------------------------------
# Cross target Component build specific configuration
#----------------------------------------------------
if(CLR_CROSS_COMPONENTS_BUILD)
  add_definitions(-DCROSS_COMPILE)

  if(CLR_CMAKE_HOST_ARCH_AMD64 AND (CLR_CMAKE_TARGET_ARCH_ARM OR CLR_CMAKE_TARGET_ARCH_I386))
    set(FEATURE_CROSSBITNESS 1)
  endif(CLR_CMAKE_HOST_ARCH_AMD64 AND (CLR_CMAKE_TARGET_ARCH_ARM OR CLR_CMAKE_TARGET_ARCH_I386))
endif(CLR_CROSS_COMPONENTS_BUILD)

#-------------------
# Enable PGO support
#-------------------
include(pgosupport.cmake)

#---------------------------------------------------
# Define sub-component targets for the build
#---------------------------------------------------
include(components.cmake)

#---------------------------
# Build the single file host
#---------------------------
if(NOT CLR_CROSS_COMPONENTS_BUILD AND NOT CLR_CMAKE_TARGET_MACCATALYST AND NOT CLR_CMAKE_TARGET_IOS AND NOT CLR_CMAKE_TARGET_TVOS AND NOT CLR_CMAKE_TARGET_ARCH_WASM)
  set(CLR_SINGLE_FILE_HOST_ONLY 1)
  add_subdirectory(${CLR_SRC_NATIVE_DIR}/corehost/apphost/static Corehost.Static)
  add_dependencies(runtime singlefilehost)
endif()
#-------------------------
# Enable C++ EH with SEH
#-------------------------
if (MSVC)
  set_property(DIRECTORY PROPERTY CLR_EH_OPTION /EHa) # enable C++ EH (w/ SEH exceptions)
endif()

#-------------------------------
# Include libraries native shims
#-------------------------------
if(NOT CLR_CROSS_COMPONENTS_BUILD)
  set(STATIC_LIBS_ONLY 1)
  add_subdirectory(${CLR_SRC_NATIVE_DIR}/libs libs-native)
endif()

#-----------------------------------------
# Add Projects
#     - project which require platform header not clr's
#     - do not depend on clr's compile definitions
#-----------------------------------------
if(CLR_CMAKE_HOST_UNIX)
    if(CLR_CMAKE_TARGET_ANDROID AND NOT DEFINED CMAKE_ANDROID_NDK)
        find_library(LZMA NAMES lzma)
        if(LZMA STREQUAL LZMA-NOTFOUND)
           message(FATAL_ERROR "Cannot find liblzma.")
        endif(LZMA STREQUAL LZMA-NOTFOUND)

        find_library(ANDROID_GLOB NAMES android-glob)
        if(ANDROID_GLOB STREQUAL ANDROID_GLOB-NOTFOUND)
          message(FATAL_ERROR "Cannot find android-glob.")
        endif()
    endif()

    if(CLR_CMAKE_TARGET_LINUX AND (CLR_CMAKE_TARGET_ARCH_I386 OR CLR_CMAKE_TARGET_ARCH_RISCV64))
      add_linker_flag(-Wl,-z,notext)
    endif()

    add_subdirectory(pal)

    if(NOT CLR_CMAKE_HOST_MACCATALYST AND NOT CLR_CMAKE_HOST_IOS AND NOT CLR_CMAKE_HOST_TVOS)
      add_subdirectory(hosts)
    endif()
else()
    if(CLR_CMAKE_TARGET_UNIX)
        add_subdirectory(${CLR_SRC_NATIVE_DIR}/external/libunwind_extras ${CLR_ARTIFACTS_OBJ_DIR}/external/libunwind)
    endif()
endif()

#----------------------------------------------------
# Build the test watchdog alongside the CLR
#----------------------------------------------------
if(NOT CLR_CMAKE_HOST_MACCATALYST AND NOT CLR_CMAKE_HOST_IOS AND NOT CLR_CMAKE_HOST_TVOS)
  add_subdirectory("${CLR_SRC_NATIVE_DIR}/watchdog" test-watchdog)
endif()

# Add this subdir. We install the headers for the jit.
add_subdirectory(pal/prebuilt/inc)

# These need to happen before the VM and debug-pal includes.
set(EP_GENERATED_HEADER_PATH "${GENERATED_INCLUDE_DIR}")
include (${CLR_SRC_NATIVE_DIR}/eventpipe/configure.cmake)

add_subdirectory(${CLR_SRC_NATIVE_DIR}/containers containers)
add_subdirectory(${CLR_SRC_NATIVE_DIR}/eventpipe eventpipe)
add_subdirectory(${CLR_SRC_NATIVE_DIR}/minipal shared_minipal)

add_subdirectory(debug/debug-pal)

add_subdirectory(minipal)

if(CLR_CMAKE_TARGET_WIN32)
  add_subdirectory(gc/sample)
endif()

#-------------------------------------
# Include directory directives
#-------------------------------------
# Include the basic prebuilt headers - required for getting fileversion resource details.
include_directories("pal/prebuilt/inc")
include_directories(${CLR_ARTIFACTS_OBJ_DIR})

if (NOT CLR_CMAKE_TARGET_BROWSER)
  add_subdirectory(tools/aot/jitinterface)
endif (NOT CLR_CMAKE_TARGET_BROWSER)

if(NOT CLR_CROSS_COMPONENTS_BUILD)
  # NativeAOT only buildable for a subset of CoreCLR-supported configurations
  if(CLR_CMAKE_HOST_ARCH_ARM64 OR CLR_CMAKE_HOST_ARCH_AMD64 OR CLR_CMAKE_HOST_ARCH_ARM OR CLR_CMAKE_HOST_ARCH_LOONGARCH64 OR CLR_CMAKE_HOST_ARCH_RISCV64 OR (CLR_CMAKE_HOST_ARCH_I386 AND CLR_CMAKE_HOST_WIN32))
    add_subdirectory(nativeaot)
  endif()
endif(NOT CLR_CROSS_COMPONENTS_BUILD)

# Above projects do not build with these compile options
# All of the compiler options are specified in file compileoptions.cmake
# Do not add any new options here. They should be added in compileoptions.cmake
if(CLR_CMAKE_HOST_WIN32)
  add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/Zl>) # omit default library name in .OBJ
endif(CLR_CMAKE_HOST_WIN32)

#--------------------------------
# Definition directives
#  - all clr specific compile definitions should be included in this file
#  - all clr specific feature variable should also be added in this file
#----------------------------------
include(clrdefinitions.cmake)

if(FEATURE_STANDALONE_GC)
  add_definitions(-DFEATURE_STANDALONE_GC)
endif(FEATURE_STANDALONE_GC)

add_subdirectory(gc)

if (CLR_CMAKE_HOST_UNIX)
  include_directories("pal/inc")
  include_directories("pal/inc/rt")
  include_directories("pal/src/safecrt")
endif (CLR_CMAKE_HOST_UNIX)

#------------------------------
# Add Product Directories
#------------------------------
include_directories("inc")
include_directories("debug/inc")
include_directories("debug/inc/${ARCH_SOURCES_DIR}")
include_directories("debug/inc/dump")
include_directories("md/inc")
include_directories("classlibnative/bcltype")
include_directories("classlibnative/inc")
include_directories("${GENERATED_INCLUDE_DIR}")
include_directories("hosts/inc")
include_directories("minipal")

if(FEATURE_INTERPRETER)
add_subdirectory(interpreter)
endif()

if(CLR_CMAKE_TARGET_WIN32 AND FEATURE_EVENT_TRACE)
    include_directories("${GENERATED_INCLUDE_DIR}/etw")
endif(CLR_CMAKE_TARGET_WIN32 AND FEATURE_EVENT_TRACE)

add_subdirectory(debug/dbgutil)

if(CLR_CMAKE_HOST_UNIX)
  if(CLR_CMAKE_HOST_OSX OR (CLR_CMAKE_HOST_LINUX AND NOT CLR_CMAKE_HOST_UNIX_X86 AND NOT CLR_CMAKE_HOST_ANDROID))
    add_subdirectory(debug/createdump)
  endif(CLR_CMAKE_HOST_OSX OR (CLR_CMAKE_HOST_LINUX AND NOT CLR_CMAKE_HOST_UNIX_X86 AND NOT CLR_CMAKE_HOST_ANDROID))

  # The CoreCLR PAL used to redefine NULL, which caused a number of null conversion and arithmetic
  # warnings and errors to be suppressed.
  # Suppress these warnings here to avoid breaking the build.
  add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wno-null-arithmetic>)
  add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-sync-alignment>)
  add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-conversion-null>)
  add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-pointer-arith>)

  set (NATIVE_RESOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/nativeresources)
  include_directories(${NATIVE_RESOURCE_DIR})

  # Win32 may be false when cross compiling
  if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set (PROCESS_RC_SCRIPT ${NATIVE_RESOURCE_DIR}/processrc.ps1)
  else()
    set (PROCESS_RC_SCRIPT ${NATIVE_RESOURCE_DIR}/processrc.sh)
  endif()
  set (RESOURCE_STRING_HEADER_DIR ${NATIVE_RESOURCE_DIR})

  # Create a command to create a C++ source file containing an array of
  # NativeStringResource structs which represent the information from a
  # given Windows .rc file. The target C++ file path is returned in the
  # variable specified by the TARGET_FILE parameter.
  function(build_resources SOURCE TARGET_NAME TARGET_FILE)
    set_property(SOURCE ${SOURCE} APPEND PROPERTY COMPILE_DEFINITIONS "RC_INVOKED")
    set(PREPROCESSED_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.rc.i)

    preprocess_file(${SOURCE} ${PREPROCESSED_SOURCE})

    set(RESOURCE_ENTRY_ARRAY_CPP ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.cpp)

    # Win32 may be false when cross compiling
    if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
      add_custom_command(
        OUTPUT ${RESOURCE_ENTRY_ARRAY_CPP}
        # Convert the preprocessed .rc file to a C++ file which will be used to make a static lib.
        COMMAND powershell -NoProfile -ExecutionPolicy ByPass -File \"${PROCESS_RC_SCRIPT}\" ${PREPROCESSED_SOURCE} ${TARGET_NAME} >${RESOURCE_ENTRY_ARRAY_CPP}
        DEPENDS ${PREPROCESSED_SOURCE} ${PROCESS_RC_SCRIPT}
      )
    else()
      add_custom_command(
        OUTPUT ${RESOURCE_ENTRY_ARRAY_CPP}
        # Convert the preprocessed .rc file to a C++ file which will be used to make a static lib.
        COMMAND ${PROCESS_RC_SCRIPT} ${PREPROCESSED_SOURCE} ${TARGET_NAME} >${RESOURCE_ENTRY_ARRAY_CPP}
        DEPENDS ${PREPROCESSED_SOURCE} ${PROCESS_RC_SCRIPT}
      )
    endif()

    include_directories(${RESOURCE_STRING_HEADER_DIR})
    set(${TARGET_FILE} ${RESOURCE_ENTRY_ARRAY_CPP} PARENT_SCOPE)
  endfunction()

  add_subdirectory(nativeresources)
endif(CLR_CMAKE_HOST_UNIX)

add_subdirectory(utilcode)
add_subdirectory(inc)

if (CLR_CMAKE_BUILD_TOOLS)
  add_subdirectory(ilasm)
  add_subdirectory(ildasm)
endif(CLR_CMAKE_BUILD_TOOLS)
add_subdirectory(gcinfo)

if (NOT CLR_CMAKE_TARGET_ARCH_WASM)
  add_subdirectory(jit)
endif()

add_subdirectory(vm)
add_subdirectory(md)

if (NOT CLR_CMAKE_TARGET_ARCH_WASM)
  add_subdirectory(debug)
endif()

add_subdirectory(binder)
add_subdirectory(classlibnative)
add_subdirectory(dlls)

if (NOT CLR_CMAKE_TARGET_ARCH_WASM)
  add_subdirectory(unwinder)
  add_subdirectory(interop)
endif()

if(NOT CLR_CMAKE_HOST_MACCATALYST AND NOT CLR_CMAKE_HOST_IOS AND NOT CLR_CMAKE_HOST_TVOS)
  add_subdirectory(tools)
endif(NOT CLR_CMAKE_HOST_MACCATALYST AND NOT CLR_CMAKE_HOST_IOS AND NOT CLR_CMAKE_HOST_TVOS)

if(CLR_CMAKE_HOST_WIN32)
  add_subdirectory(hosts)
endif(CLR_CMAKE_HOST_WIN32)
