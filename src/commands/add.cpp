#include "commands/add.h"
#include "core/ignore.h"
#include "utils/file_io.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

void cmd_add(const std::string &filename)
{
    fs::path source = filename;

    if (!fs::exists(source))
    {
        std::cout << "Error: file '" << filename << "' does not exist\n";
        return;
    }

    auto rules = load_ignore_rules();
    std::string relpath = filename;
    std::replace(relpath.begin(), relpath.end(), '\\', '/');
    if (matches_ignore_rule(relpath, rules))
    {
        std::cout << "Ignored by .mygitignore: '" << filename << "'\n";
        return;
    }

    fs::path dest = fs::path(".my_git/staging") / filename;
    if (dest.has_parent_path())
        fs::create_directories(dest.parent_path());
    fs::copy_file(source, dest, fs::copy_options::overwrite_existing);

    auto staged = read_lines(".my_git/index");
    bool already_staged = false;
    for (const auto &f : staged)
        if (f == filename)
        {
            already_staged = true;
            break;
        }

    if (!already_staged)
    {
        std::ofstream out(".my_git/index", std::ios::app);
        out << filename << "\n";
    }

    std::cout << "Staged '" << filename << "'\n";
}
