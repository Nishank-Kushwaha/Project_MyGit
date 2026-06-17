#include "commands/checkout.h"
#include "core/refs.h"
#include "core/object_store.h"
#include "core/ignore.h"
#include "utils/file_io.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <set>

namespace fs = std::filesystem;

void cmd_checkout(const std::string &name)
{
    fs::path ref_path = fs::path(".my_git/refs") / name;
    fs::path commit_path = fs::path(".my_git/commits") / name;

    std::string target_commit;
    bool is_branch = fs::exists(ref_path);
    bool is_commit = fs::exists(commit_path);

    if (is_branch)
    {
        target_commit = read_file(ref_path);
    }
    else if (is_commit)
    {
        target_commit = name; // raw hash, detached HEAD
    }
    else
    {
        std::cout << "Error: '" << name << "' is not a known branch or commit\n";
        return;
    }

    std::string current_commit = get_head_commit();

    // CHANGED: reconstruct both snapshots from objects instead of reading files/
    std::map<std::string, std::string> current_snapshot;
    if (!current_commit.empty())
        current_snapshot = reconstruct_commit(current_commit);
    std::map<std::string, std::string> target_snapshot = reconstruct_commit(target_commit);

    // --- Remove files tracked by current commit but NOT in target commit ---
    for (auto &[filename, content] : current_snapshot)
    {
        if (target_snapshot.count(filename) == 0)
        {
            fs::path p(filename);
            fs::remove(filename);
            if (p.has_parent_path())
                remove_empty_dirs_upward(p.parent_path(), fs::path(".")); // NEW
        }
    }

    // --- Restore files from target commit's snapshot ---
    for (auto &[filename, content] : target_snapshot)
    {
        fs::path p(filename);
        if (p.has_parent_path())
            fs::create_directories(p.parent_path()); // forward-compat for nested paths
        write_file(filename, content);
    }

    // --- Update HEAD ---
    if (is_branch)
    {
        write_file(".my_git/HEAD", "ref: refs/" + name);
        std::cout << "Switched to branch '" << name << "'\n";
    }
    else
    {
        write_file(".my_git/HEAD", target_commit); // raw hash = detached
        std::cout << "Note: switching to '" << target_commit.substr(0, 7) << "'.\n";
        std::cout << "You are in 'detached HEAD' state. Any new commits made now\n";
        std::cout << "may not belong to any branch.\n";
    }
}
