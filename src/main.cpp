#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

void cmd_init()
{
    fs::create_directories(".my_git/commits");
    fs::create_directories(".my_git/staging");
    std::ofstream(".my_git/HEAD").close();
    std::ofstream(".my_git/index").close();
    std::cout << "Initialized empty my_git repository\n";
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: my_git <command>\n";
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "init")
        cmd_init();
    else if (cmd == "help")
        std::cout << "Commands: init, add, commit, log, status\n";
    else
        std::cout << "Unknown command: " << cmd << "\n";

    return 0;
}