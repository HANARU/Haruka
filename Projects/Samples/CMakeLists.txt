haruka_add_project(
    NAME          Samples
    PROJECT_DIR   ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES       ${CMAKE_CURRENT_SOURCE_DIR}/Source/SamplesGameModule.cpp
    EDITOR_TARGET HarukaEngine
    GAME_TARGET   HarukaGame
)
