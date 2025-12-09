# Test executable
add_executable(nerd-dictation-tests
    test_main.cpp
    test_backend_wrapper.cpp
    test_postprocess.cpp
    test_input_simulator.cpp
    test_config.cpp
    test_daemon.cpp
)

# Include directories
target_include_directories(nerd-dictation-tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${NERD_DICTATION_API_INCLUDE}
)

# Link with main project libraries and dependencies
target_link_libraries(nerd-dictation-tests
    PRIVATE
    nerd-dictation-dummy
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    ${CMAKE_DL_LIBS}
)

# Link with spdlog if available
if(SPDLOG_FOUND)
    if(TARGET spdlog::spdlog)
        target_link_libraries(nerd-dictation-tests PRIVATE spdlog::spdlog)
    endif()
endif()

# Set environment variable for tests to find dummy backend
# Use generator expression to get the full path to the built library
set_target_properties(nerd-dictation-tests PROPERTIES
    ENVIRONMENT "NERD_DICTATION_DUMMY_BACKEND_PATH=$<TARGET_FILE:nerd-dictation-dummy>;CMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
)

# Add test to CTest
include(GoogleTest)
gtest_discover_tests(nerd-dictation-tests)
