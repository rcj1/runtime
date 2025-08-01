set(CMAKE_INCLUDE_CURRENT_DIR ON)

# So simdhash will build correctly without a dn-config.h
add_compile_definitions(NO_CONFIG_H)

set(INTERPRETER_SOURCES
  compiler.cpp
  compileropt.cpp
  intops.cpp
  interpconfig.cpp
  eeinterp.cpp
  stackmap.cpp
  naming.cpp
  methodset.cpp
  intrinsics.cpp
  ../../native/containers/dn-simdhash.c
  ../../native/containers/dn-simdhash-ght-compatible.c
  ../../native/containers/dn-simdhash-ptr-ptr.c)

set(INTERPRETER_LINK_LIBRARIES
  gcinfo
)

if(CLR_CMAKE_HOST_WIN32)
    list(APPEND INTERPRETER_LINK_LIBRARIES
        ${STATIC_MT_CRT_LIB}
        ${STATIC_MT_VCRT_LIB}
    )
endif(CLR_CMAKE_HOST_WIN32)

if(CLR_CMAKE_HOST_WIN32)
    set(CLRINTERPRETER_EXPORTS ${CMAKE_CURRENT_LIST_DIR}/clrinterpreter.exports)
    set(EXPORTS_FILE ${CMAKE_CURRENT_BINARY_DIR}/clrinterpreter.def)
    preprocess_file(${CLRINTERPRETER_EXPORTS} ${EXPORTS_FILE})
    list(APPEND INTERPRETER_SOURCES ${EXPORTS_FILE})
    add_custom_target(interpreter_exports DEPENDS ${EXPORTS_FILE})
else()
    set(CLRINTERPRETER_EXPORTS ${CMAKE_CURRENT_LIST_DIR}/clrinterpreter_unixexports.src)
    set(EXPORTS_FILE ${CMAKE_CURRENT_BINARY_DIR}/clrinterpreter.exports)
    generate_exports_file(${CLRINTERPRETER_EXPORTS} ${EXPORTS_FILE})
    add_custom_target(interpreter_exports DEPENDS ${EXPORTS_FILE})
endif()

if(FEATURE_STATICALLY_LINKED AND NOT FEATURE_JIT)
  set(LIBRARY_TYPE STATIC)
else()
  set(LIBRARY_TYPE SHARED)
endif()

add_library_clr(clrinterpreter ${LIBRARY_TYPE} ${INTERPRETER_SOURCES})

if (NOT CMAKE_GENERATOR MATCHES "Visual Studio")
  set_target_properties(clrinterpreter PROPERTIES EXCLUDE_FROM_ALL $<NOT:${FEATURE_INTERPRETER}>)
endif()

add_dependencies(clrinterpreter interpreter_exports)

if(NOT CLR_CMAKE_HOST_WIN32)
    set_exports_linker_option(${EXPORTS_FILE})
    set_property(TARGET clrinterpreter APPEND_STRING PROPERTY LINK_FLAGS ${EXPORTS_LINKER_OPTION})
endif()

target_link_libraries(clrinterpreter
        PRIVATE
        ${INTERPRETER_LINK_LIBRARIES}
    )

set_property(TARGET clrinterpreter APPEND_STRING PROPERTY LINK_DEPENDS ${EXPORTS_FILE})

install_clr(TARGETS clrinterpreter DESTINATIONS . sharedFramework COMPONENT runtime OPTIONAL)
