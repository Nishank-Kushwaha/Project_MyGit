#include "commands/diff.h"
#include "core/object_store.h"
#include "core/diff_engine.h"
#include "utils/file_io.h"
#include <filesystem>
#include <iostream>
#include <set>
#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;

void cmd_diff(const std::string &hash1, const std::string &hash2)
{
    if (!fs::exists(fs::path(".my_git/commits") / hash1 / "metadata") ||
        !fs::exists(fs::path(".my_git/commits") / hash2 / "metadata"))
    {
        std::cout << "Error: one or both commit hashes not found\n";
        return;
    }

    auto snap1 = reconstruct_commit(hash1); // old
    auto snap2 = reconstruct_commit(hash2); // new

    // Union of filenames (std::map keeps them sorted already)
    std::set<std::string> all_filenames;
    for (auto &[name, _] : snap1)
        all_filenames.insert(name);
    for (auto &[name, _] : snap2)
        all_filenames.insert(name);

    for (const auto &fname : all_filenames)
    {
        std::string old_content = snap1.count(fname) ? snap1[fname] : ""; // "" if file didn't exist
        std::string new_content = snap2.count(fname) ? snap2[fname] : ""; // "" if file didn't exist

        if (old_content == new_content)
            continue; // no change in this file

        std::cout << "diff --my_git a/" << fname << " b/" << fname << "\n";

        auto a = split_lines(old_content);
        auto b = split_lines(new_content);
        auto diff = diff_lines(a, b);

        for (const auto &dl : diff)
        {
            if (dl.type == ' ')
                std::cout << "  " << dl.text << "\n";
            else
                std::cout << dl.type << " " << dl.text << "\n";
        }
        std::cout << "\n";
    }
}
