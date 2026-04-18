cmake_minimum_required(VERSION 3.16)

project(VibePulseHeartRate VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest) # provides BUILD_TESTING and calls enable_testing()

find_package(Threads REQUIRED)
find_library(GPIOD_LIBRARY gpiod REQUIRED)

if(NOT GPIOD_LIBRARY)
    message(FATAL_ERROR "libgpiod not found. Install libgpiod development files on the target system.")
endif()

# Audio is a core project feature (heart-rate–driven adaptive music).
# Set OFF only for sensor-only builds or environments without SDL2/SDL2_mixer.
option(VIBEPULSE_ENABLE_AUDIO "Enable SDL2/SDL2_mixer audio playback" ON)

# ── Core modules (portable, unit-test friendly) ───────────────
add_library(vibepulse_core
    src/MusicPlayer.cpp
    src/ZoneMusicPlayer.cpp
)

target_compile_features(vibepulse_core PUBLIC cxx_std_17)
target_include_directories(vibepulse_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vibepulse_core PUBLIC Threads::Threads)

# ── Hardware / sensor pipeline (MAX30102 + DSP + BPM) ─────────
add_library(vibepulse_hw
    src/HeartRateCalculator.cpp
    src/Sensor.cpp
    src/dsp.cpp
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

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
