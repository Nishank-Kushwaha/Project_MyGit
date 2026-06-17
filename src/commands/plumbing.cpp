#include "commands/plumbing.h"
#include "core/object_store.h"
#include "utils/file_io.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;

void cmd_hash_object(const std::string &filename)
{
    if (!fs::exists(filename))
    {
        std::cout << "Error: file '" << filename << "' does not exist\n";
        return;
    }
    std::string content = read_file(filename);
    std::string hash = write_object(content, "blob");
    std::cout << hash << "\n";
}

void cmd_cat_file(const std::string &hash)
{
    std::string raw = read_object(hash);
    if (raw.empty())
    {
        std::cout << "Error: object '" << hash << "' not found\n";
        return;
    }
    // Strip "<type> <size>\0" header
    size_t null_pos = raw.find('\0');
    if (null_pos == std::string::npos)
    {
        std::cout << "Error: malformed object\n";
        return;
    }
    std::cout << raw.substr(null_pos + 1);
}

void cmd_write_tree()
{
    std::string hash = write_tree(".");
    std::cout << hash << "\n";
}

void cmd_ls_tree(const std::string &hash)
{
    std::string raw = read_object(hash);
    if (raw.empty())
    {
        std::cout << "Error: object '" << hash << "' not found\n";
        return;
    }
    size_t null_pos = raw.find('\0');
    std::cout << raw.substr(null_pos + 1); // print entries as-is
}
