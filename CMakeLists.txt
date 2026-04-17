cmake_minimum_required(VERSION 3.16)

project(VibePulseHeartRate VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_library(GPIOD_LIBRARY gpiod REQUIRED)

if(NOT GPIOD_LIBRARY)
    message(FATAL_ERROR "libgpiod not found. Install libgpiod development files on the target system.")
endif()

add_executable(vibepulse_hr
    src/main.cpp
    src/HeartRateCalculator.cpp
    src/Sensor.cpp
    src/dsp.cpp
)

target_include_directories(vibepulse_hr
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vibepulse_hr
    PRIVATE
        Threads::Threads
        ${GPIOD_LIBRARY}
)
