cmake_minimum_required(VERSION 3.15)
project(RetroStudio C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
    "src/*.c"
)

# Retro68 target executable setup
add_application(RetroStudio ${SOURCES}
    src/RetroStudio.r
    CREATOR "RSTD"
    TYPE "APPL"
)

target_include_directories(RetroStudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Navigation Services stubs (NavGetFile, NavPutFile, etc.)
target_link_libraries(RetroStudio NavigationLib)

