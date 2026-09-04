##################################################################
#
# Coverage Unit Test build recipe
#
# This CMake file contains the recipe for building the payload_if unit tests.
# It is invoked from the parent directory when unit tests are enabled.
#
##################################################################
#
#
# NOTE on the subdirectory structures here:
#
# - "inc" provides local header files shared between the coveragetest,
#    wrappers, and overrides source code units
# - "coveragetest" contains source code for the actual unit test cases
#    The primary objective is to get line/path coverage on the FSW 
#    code units.
#
add_cfe_coverage_stubs(payload_if-internal stubs/payload_if_device_stubs.c stubs/libuart_stubs.c) 
target_link_libraries(coverage-payload_if-internal-stubs ut_core_api_stubs ut_assert)
target_include_directories(coverage-payload_if-internal-stubs PUBLIC $<TARGET_PROPERTY:payload_if,INCLUDE_DIRECTORIES>)
 
# Use the UT assert public API, and allow direct
# inclusion of source files that are normally private
include_directories(${PROJECT_SOURCE_DIR}/fsw/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/inc)
include_directories(${hwlib_MISSION_DIR}/fsw/public_inc)
include_directories(${PROJECT_SOURCE_DIR}/../../../../fsw/osal/ut_assert/inc)
# Add a coverage test executable called "payload_if-ALL" that 
# covers all of the functions in payload_if_app.  
#
# Also note in a more complex app/lib the coverage test can also
# be broken down into smaller units (in which case one should use
# a unique suffix other than "ALL" for each unit).  For example,
# OSAL implements a separate coverage test per source unit.
add_cfe_coverage_test(payload_if ALL 
    "coveragetest/coveragetest_payload_if_app.c"
    "../src/payload_if_app.c"
)
add_cfe_coverage_test(payload_if DEVICE 
    "coveragetest/coveragetest_payload_if_device.c"
    "../../shared/payload_if_device.c"
    "stubs/libuart_stubs.c"
)

# Add a coverage test executable called "payload_if-CCSDS" that verifies
# the real CFE_MSG_* implementation correctly builds CCSDS Space Packets.
#
# IMPORTANT: This target intentionally does NOT get the payload_if-internal
# coverage dependency (unlike ALL, below). That dependency links in stubbed
# CFE_MSG_* functions via ut_core_api_stubs, which would defeat the purpose
# of this test -- we specifically need to link and verify the REAL
# cfe_msg_ccsdspri.c implementation, not a stub standing in for it.
add_cfe_coverage_test(payload_if CCSDS
    "coveragetest/coveragetest_payload_if_ccsds.c"
    "${PROJECT_SOURCE_DIR}/../../../../fsw/cfe/modules/msg/fsw/src/cfe_msg_ccsdspri.c"
)

# Link the real payload-link library into the ALL test target, since
# payload_if_app.c now calls plframe_decode_init/plframe_decode_feed directly.
target_link_libraries(coverage-payload_if-ALL-testrunner payload_link)
target_link_libraries(coverage-payload_if-ALL-testrunner STAR::payload_apids)

add_cfe_coverage_dependency(payload_if ALL payload_if-internal)
# The payload_if uses library functions provided by payload_if_lib so must be linked
# with the payload_if_lib stub library (this is mainly just an example of how this 
# can be done).
#add_cfe_coverage_dependency(payload_if ALL payload_if_lib)
