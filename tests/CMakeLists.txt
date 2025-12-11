# Test executable
# Include source files needed for testing
set(TEST_SOURCES
    test_main.cpp
    test_backend_wrapper.cpp
    test_postprocess.cpp
    test_input_simulator.cpp
    test_config.cpp
    test_daemon.cpp
    test_wake_word.cpp
    # Source files for components being tested
    ${CMAKE_SOURCE_DIR}/src/input/input_simulator.cpp
    ${CMAKE_SOURCE_DIR}/src/input/postprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/config/config.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/daemon.cpp
    ${CMAKE_SOURCE_DIR}/src/audio/audio_preprocessor.cpp
    ${CMAKE_SOURCE_DIR}/src/sounds/sounds.cpp
)

add_executable(nerd-dictation-tests ${TEST_SOURCES})

# Include directories
target_include_directories(nerd-dictation-tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${NERD_DICTATION_API_INCLUDE}
    ${PIPEWIRE_INCLUDE_DIRS}
    ${PULSE_SIMPLE_INCLUDE_DIRS}
    ${WEBRTC_APM_INCLUDE_DIRS}
    ${SAMPLERATE_INCLUDE_DIRS}
)

# TOML11, CLI11, Eigen includes
if(TOML11_FOUND)
    target_include_directories(nerd-dictation-tests PRIVATE ${TOML11_INCLUDE})
endif()
if(CLI11_FOUND)
    target_include_directories(nerd-dictation-tests PRIVATE ${CLI11_INCLUDE})
endif()
if(TARGET Eigen3::Eigen)
    # Eigen is header-only, handled via include
else()
    find_package(Eigen3 REQUIRED)
    target_include_directories(nerd-dictation-tests PRIVATE ${EIGEN3_INCLUDE_DIR})
endif()

# Link with main project libraries and dependencies
# NOTE: Do NOT link against any backend (dummy, t-one, etc.) to avoid symbol conflicts
# when loading different backends via dlopen at runtime
target_link_libraries(nerd-dictation-tests
    PRIVATE
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    ${CMAKE_DL_LIBS}
    ${PIPEWIRE_LIBRARIES}
    ${WEBRTC_APM_LIBRARIES}
    ${SAMPLERATE_LIBRARIES}
)

# PulseAudio Simple API (for sound notifications)
if(PULSE_SIMPLE_FOUND)
    target_link_libraries(nerd-dictation-tests PRIVATE ${PULSE_SIMPLE_LIBRARIES})
endif()

# Link with spdlog if available
if(SPDLOG_FOUND)
    if(TARGET spdlog::spdlog)
        target_link_libraries(nerd-dictation-tests PRIVATE spdlog::spdlog)
    endif()
endif()

# Compile definitions
target_compile_definitions(nerd-dictation-tests PRIVATE HAVE_EIGEN HAVE_EMBEDDED_SOUNDS=0)
if(PULSE_SIMPLE_FOUND)
    target_compile_definitions(nerd-dictation-tests PRIVATE HAVE_PULSEAUDIO)
endif()

# Compile options
target_compile_options(nerd-dictation-tests
    PRIVATE
    ${PIPEWIRE_CFLAGS_OTHER}
    ${WEBRTC_APM_CFLAGS_OTHER}
)

# Add test to CTest
# Set environment variables for tests
# Use semicolon-separated string (not CMake list) for ENVIRONMENT property
set(TEST_ENV_STRING "NERD_DICTATION_DUMMY_BACKEND_PATH=$<TARGET_FILE:nerd-dictation-dummy>\;CMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}\;TEST_SAMPLES_DIR=${CMAKE_SOURCE_DIR}/test-samples\;LD_LIBRARY_PATH=${CMAKE_SOURCE_DIR}/install/lib:${CMAKE_SOURCE_DIR}/install/lib/res_embed")

include(GoogleTest)
gtest_discover_tests(nerd-dictation-tests
    PROPERTIES ENVIRONMENT "${TEST_ENV_STRING}"
)
