add_executable(test_music_player
    test_music_player.cpp
)

target_compile_features(test_music_player PRIVATE cxx_std_17)
target_include_directories(test_music_player PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(test_music_player PRIVATE vibepulse_core Threads::Threads)

add_test(NAME MusicPlayerTests COMMAND test_music_player)

# Hardware smoke tests (I2C/MAX30102) are intentionally NOT registered by default,
# because they require physical wiring and root/device access.
# If you want to run them via CTest, we can add an option gate later.

