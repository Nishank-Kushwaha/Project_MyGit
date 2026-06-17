#include "commands/init.h"
#include "utils/file_io.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

void cmd_init()
{
    fs::create_directories(".my_git/commits");
    fs::create_directories(".my_git/staging");
    fs::create_directories(".my_git/refs");
    write_file(".my_git/HEAD", "ref: refs/main");
    if (!fs::exists(".my_git/refs/main"))
        write_file(".my_git/refs/main", "");
    write_file(".my_git/index", "");
    std::cout << "Initialized empty my_git repository\n";
}
