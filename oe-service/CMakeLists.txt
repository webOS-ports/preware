cmake_minimum_required(VERSION 2.8.7)

project(org.webosports.service.ipkg C)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC2)
webos_component(2 0 0)

include(FindPkgConfig)

pkg_check_modules(GLIB2 REQUIRED glib-2.0)
include_directories(${GLIB2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GLIB2_CFLAGS})

pkg_check_modules(LUNASERVICE2 REQUIRED luna-service2)
include_directories(${LUNASERVICE2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${LUNASERVICE2_CFLAGS})

pkg_check_modules(MJSON REQUIRED json-c)
include_directories(${MJSON_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${MJSON_CFLAGS})

# ipk_check.c reads the packages' gzip'd control and data archives itself,
# fetch.c unpacks the gzip'd feed indexes.
pkg_check_modules(ZLIB REQUIRED zlib)
include_directories(${ZLIB_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${ZLIB_CFLAGS})

webos_add_compiler_flags(ALL -Wall)
webos_add_linker_options(ALL --no-undefined)

add_definitions(-DVERSION=\"2.0.0\")

file(GLOB SOURCE_FILES src/*.c)

add_executable(org.webosports.service.ipkg ${SOURCE_FILES})
target_link_libraries(org.webosports.service.ipkg
    ${GLIB2_LDFLAGS} ${LUNASERVICE2_LDFLAGS} ${MJSON_LDFLAGS} ${ZLIB_LDFLAGS}
    rt pthread)

# Unit tests for the package checks: -DBUILD_TESTS=ON, then ctest (or run
# test_ipk_check, also on the device).
option(BUILD_TESTS "Build the unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_executable(test_ipk_check tests/test_ipk_check.c src/ipk_check.c)
    target_include_directories(test_ipk_check PRIVATE src)
    target_link_libraries(test_ipk_check ${ZLIB_LDFLAGS})
    add_test(NAME ipk-check COMMAND test_ipk_check)
    add_executable(test_fetch tests/test_fetch.c src/fetch.c)
    target_include_directories(test_fetch PRIVATE src)
    target_link_libraries(test_fetch ${ZLIB_LDFLAGS} pthread)
    add_test(NAME fetch COMMAND test_fetch)
    # Includes luna_methods.c: the service's own process runner.
    add_executable(test_run_argv tests/test_run_argv.c src/ipk_check.c src/fetch.c src/luna_service.c)
    target_include_directories(test_run_argv PRIVATE src)
    target_compile_options(test_run_argv PRIVATE -fcommon)
    target_link_libraries(test_run_argv
        ${GLIB2_LDFLAGS} ${LUNASERVICE2_LDFLAGS} ${MJSON_LDFLAGS} ${ZLIB_LDFLAGS} rt pthread)
    add_test(NAME run-argv COMMAND test_run_argv)
endif()

webos_build_daemon()
webos_build_system_bus_files()
