#include "commands/branch.h"
#include "core/refs.h"
#include "utils/file_io.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;

void cmd_branch(const std::string &name)
{
    fs::path ref_path = fs::path(".my_git/refs") / name;

    if (fs::exists(ref_path))
    {
        std::cout << "Error: branch '" << name << "' already exists\n";
        return;
    }

    std::string current_commit = get_head_commit();
    write_file(ref_path, current_commit);
    std::cout << "Created branch '" << name << "' at " << current_commit.substr(0, 7) << "\n";
}

void cmd_branch_list()
{
    std::string current_ref = get_current_branch_ref(); // e.g. "refs/main", or "" if detached

    for (const auto &entry : fs::directory_iterator(".my_git/refs"))
    {
        if (!entry.is_regular_file())
            continue; // <-- skip "remotes/" subfolder
        std::string branch_name = entry.path().filename().string();
        std::string this_ref = "refs/" + branch_name;

        if (this_ref == current_ref)
        {
            std::cout << "* " << branch_name << "\n";
        }
        else
        {
            std::cout << "  " << branch_name << "\n";
        }
    }

    if (current_ref.empty())
    {
        std::string head_hash = get_head_commit();
        std::cout << "* (HEAD detached at " << head_hash.substr(0, 7) << ")\n";
    }
}
