#include "commands/status.h"
#include "core/refs.h"
#include "core/object_store.h"
#include "core/ignore.h"
#include "utils/file_io.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <set>
#include <vector>
#include <sstream>

namespace fs = std::filesystem;

void cmd_status()
{
    auto staged = read_lines(".my_git/index");
    std::string head = get_head_commit();

    // NEW: reconstruct HEAD's snapshot from objects instead of reading files/
    std::map<std::string, std::string> head_snapshot;
    if (!head.empty())
        head_snapshot = reconstruct_commit(head);

    // Helper: get relative path from project root as string with forward slashes
    auto rel_path = [](const fs::path &p) -> std::string
    {
        std::string s = fs::relative(p, ".").string();
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    };

    // Load ignore rules once, use for all entries
    auto rules = load_ignore_rules();

    auto skip_entry = [&rules](const fs::directory_entry &entry) -> bool
    {
        std::string p = entry.path().string();
        std::replace(p.begin(), p.end(), '\\', '/');

        // Strip leading "./" for consistent matching
        if (p.rfind("./", 0) == 0)
            p = p.substr(2);

        return matches_ignore_rule(p, rules);
    };

    // --- Section 1: Staged files ---
    std::cout << "Staged for commit:\n";
    if (staged.empty())
    {
        std::cout << "  (none)\n";
    }
    else
    {
        for (const auto &f : staged)
        {
            std::cout << "  " << f << "\n";
        }
    }

    // --- Section 2: Modified files (not staged) ---
    std::cout << "\nModified (Changes not staged for commit):\n";
    bool any_modified = false;

    for (const auto &entry : fs::recursive_directory_iterator("."))
    {
        if (skip_entry(entry))
            continue;
        if (!entry.is_regular_file())
            continue;

        std::string relpath = rel_path(entry.path());

        bool is_staged = false;
        for (const auto &f : staged)
        {
            if (f == relpath)
            {
                is_staged = true;
                break;
            }
        }

        bool in_last_commit = head_snapshot.count(relpath) > 0;

        if (is_staged)
        {
            fs::path staged_copy = fs::path(".my_git/staging") / relpath;
            if (!files_equal(entry.path(), staged_copy))
            {
                std::cout << "  " << relpath << "\n";
                any_modified = true;
            }
        }
        else if (in_last_commit)
        {
            std::string working_content = read_file(entry.path());
            if (working_content != head_snapshot[relpath])
            {
                std::cout << "  " << relpath << "\n";
                any_modified = true;
            }
        }
    }
    if (!any_modified)
        std::cout << "  (none)\n";

    // --- Section 3: Untracked files ---
    std::cout << "\nUntracked files:\n";
    bool any_untracked = false;

    for (const auto &entry : fs::recursive_directory_iterator("."))
    {
        if (skip_entry(entry))
            continue;
        if (!entry.is_regular_file())
            continue;

        std::string relpath = rel_path(entry.path());

        // Is it already staged?
        bool is_staged = false;
        for (const auto &f : staged)
        {
            if (f == relpath)
            {
                is_staged = true;
                break;
            }
        }

        // Was it part of the last commit?
        bool in_last_commit = head_snapshot.count(relpath) > 0;

        if (!is_staged && !in_last_commit)
        {
            std::cout << "  " << relpath << "\n";
            any_untracked = true;
        }
    }

    if (!any_untracked)
        std::cout << "  (none)\n";
}
