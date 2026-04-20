cmake_minimum_required(VERSION 3.16)

project(VibePulseHeartRate VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

find_package(Threads REQUIRED)
find_library(GPIOD_LIBRARY gpiod REQUIRED)

if(NOT GPIOD_LIBRARY)
    message(FATAL_ERROR "libgpiod not found. Install libgpiod development files on the target system.")
endif()

option(VIBEPULSE_ENABLE_AUDIO "Enable SDL2/SDL2_mixer audio playback" ON)

# ── Core music player library (portable, unit-test friendly) ──
add_library(vibepulse_core
    src/MusicPlayer.cpp
    src/ZoneMusicPlayer.cpp
)
target_compile_features(vibepulse_core PUBLIC cxx_std_17)
target_include_directories(vibepulse_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vibepulse_core PUBLIC Threads::Threads)

# ── Hardware / sensor pipeline (MAX30102  + BPM) ─────────
add_library(vibepulse_hw
    src/HeartRateCalculator.cpp
    src/Sensor.cpp
)
target_compile_features(vibepulse_hw PUBLIC cxx_std_17)
target_include_directories(vibepulse_hw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vibepulse_hw PUBLIC Threads::Threads ${GPIOD_LIBRARY})

# ── Application entrypoint ────────────────────────────────────
add_executable(vibepulse_hr src/main.cpp)
target_compile_features(vibepulse_hr PRIVATE cxx_std_17)
target_link_libraries(vibepulse_hr PRIVATE vibepulse_core vibepulse_hw)

if(VIBEPULSE_ENABLE_AUDIO)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_MIXER REQUIRED SDL2_mixer)

    target_compile_definitions(vibepulse_hr PRIVATE VIBEPULSE_ENABLE_AUDIO=1)
    target_include_directories(vibepulse_hr PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2_MIXER_INCLUDE_DIRS})
    target_link_libraries(vibepulse_hr PRIVATE ${SDL2_LIBRARIES} ${SDL2_MIXER_LIBRARIES})
endif()

# ── Unit tests (Google Test) ──────────────────────────────────
if(BUILD_TESTING)
    find_package(GTest REQUIRED)

    add_executable(run_tests src/test_MusicPlayer.cpp)
    target_compile_features(run_tests PRIVATE cxx_std_17)
    target_include_directories(run_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(run_tests PRIVATE
        vibepulse_core
        GTest::GTest
        GTest::Main
        Threads::Threads
    )

    include(GoogleTest)
    gtest_discover_tests(run_tests)

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
        add_subdirectory(tests)
    endif()
endif()
