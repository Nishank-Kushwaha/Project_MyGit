#include "commands/merge.h"
#include "core/refs.h"
#include "core/object_store.h"
#include "core/commits.h"
#include "core/diff_engine.h"
#include "utils/file_io.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <set>
#include <map>

namespace fs = std::filesystem;

void cmd_merge(const std::string &branch_name)
{
    fs::path ref_path = fs::path(".my_git/refs") / branch_name;
    if (!fs::exists(ref_path))
    {
        std::cout << "Error: branch '" << branch_name << "' does not exist\n";
        return;
    }

    std::string ours = get_head_commit();
    std::string theirs = read_file(ref_path);
    std::string base = find_merge_base(ours, theirs);

    write_file(".my_git/MERGE_HEAD", theirs);

    std::cout << "Merging '" << branch_name << "' into current branch\n";
    std::cout << "Base: " << base.substr(0, 7) << "  Ours: " << ours.substr(0, 7)
              << "  Theirs: " << theirs.substr(0, 7) << "\n\n";

    // CHANGED: reconstruct all three snapshots from objects instead of reading files/
    auto base_snapshot = base.empty() ? std::map<std::string, std::string>{} : reconstruct_commit(base);
    auto ours_snapshot = reconstruct_commit(ours);
    auto theirs_snapshot = reconstruct_commit(theirs);

    // Union of filenames across all 3 snapshots
    std::set<std::string> all_files;
    for (auto &[name, _] : base_snapshot)
        all_files.insert(name);
    for (auto &[name, _] : ours_snapshot)
        all_files.insert(name);
    for (auto &[name, _] : theirs_snapshot)
        all_files.insert(name);

    bool any_conflict = false;

    for (const auto &filename : all_files)
    {
        std::string base_content = base_snapshot.count(filename) ? base_snapshot[filename] : "";
        std::string ours_content = ours_snapshot.count(filename) ? ours_snapshot[filename] : "";
        std::string theirs_content = theirs_snapshot.count(filename) ? theirs_snapshot[filename] : "";

        if (ours_content == theirs_content)
        {
            // Identical on both sides (or both unchanged) -> nothing to do
            continue;
        }
        if (base_content == ours_content)
        {
            // Only 'theirs' changed -> take theirs
            write_file(filename, theirs_content);
            std::cout << "Updated (from " << branch_name << "): " << filename << "\n";
            continue;
        }
        if (base_content == theirs_content)
        {
            // Only 'ours' changed -> keep ours (already in working dir, but ensure it's written)
            write_file(filename, ours_content);
            std::cout << "Kept (ours): " << filename << "\n";
            continue;
        }

        // Both changed -> CONFLICT
        any_conflict = true;
        std::string merged = "<<<<<<< HEAD\n" + ours_content + "=======\n" + theirs_content + ">>>>>>> " + branch_name + "\n";
        write_file(filename, merged);
        std::cout << "CONFLICT: " << filename << "\n";
    }

    if (any_conflict)
    {
        std::cout << "\nAutomatic merge failed; fix conflicts, then run:\n";
        std::cout << "  my_git add <file>\n";
        std::cout << "  my_git commit \"merge message\"\n";
    }
    else
    {
        std::cout << "\nMerge successful (no conflicts). Run:\n";
        std::cout << "  my_git add <file>\n";
        std::cout << "  my_git commit \"merge message\"\n";
        std::cout << "to record the merge commit.\n";
    }
}
