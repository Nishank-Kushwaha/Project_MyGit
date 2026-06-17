cmake_minimum_required(VERSION 3.15)
project(my_git)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(include)

add_executable(my_git
    src/main.cpp

    src/utils/sha1.cpp
    src/utils/file_io.cpp
    src/utils/time.cpp

    src/core/ignore.cpp
    src/core/refs.cpp
    src/core/object_store.cpp
    src/core/diff_engine.cpp
    src/core/commits.cpp
    src/core/remote.cpp

    src/commands/init.cpp
    src/commands/add.cpp
    src/commands/commit.cpp
    src/commands/log.cpp
    src/commands/status.cpp
    src/commands/diff.cpp
    src/commands/branch.cpp
    src/commands/checkout.cpp
    src/commands/merge.cpp
    src/commands/graph.cpp
    src/commands/remote.cpp
    src/commands/fsck.cpp
    src/commands/plumbing.cpp
    src/commands/help.cpp
)

target_link_libraries(my_git z)
