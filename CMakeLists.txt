cmake_minimum_required(VERSION 3.16)
project(VibePulse_ENG5220 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Core library (no SDL2 dep, used by both main and tests) ───
add_library(music_player_core STATIC
    src/ZoneMusicPlayer.cpp
)
target_include_directories(music_player_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

# ── Real executable (requires SDL2, SDL2_mixer, sensor libs) ──
find_package(SDL2       QUIET)
find_package(SDL2_mixer QUIET)

if(SDL2_FOUND AND SDL2_mixer_FOUND)
    add_executable(vibepulse
        src/main.cpp
        src/Sensor.cpp
        src/HeartRateCalculator.cpp
    )
    target_include_directories(vibepulse PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(vibepulse PRIVATE
        music_player_core
        SDL2::SDL2
        SDL2_mixer::SDL2_mixer
        gpiod        # GPIO for Raspberry Pi
        pthread
    )
    message(STATUS "SDL2 found — vibepulse target enabled")
else()
    message(WARNING "SDL2/SDL2_mixer not found — skipping vibepulse target")
endif()

# ── Unit tests (Google Test, NO SDL2 / NO sensor needed) ──────
find_package(GTest REQUIRED)

add_executable(run_tests src/test_MusicPlayer.cpp)
target_include_directories(run_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(run_tests PRIVATE
    music_player_core
    GTest::GTest
    GTest::Main
    pthread
)

include(GoogleTest)
gtest_discover_tests(run_tests)
