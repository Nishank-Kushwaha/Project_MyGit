#include "commands/commit.h"
#include "core/object_store.h"
#include "core/refs.h"
#include "utils/file_io.h"
#include "utils/sha1.h"
#include "utils/time.h"
#include <filesystem>
#include <iostream>
#include <map>

namespace fs = std::filesystem;

void cmd_commit(const std::string &message)
{
    auto staged = read_lines(".my_git/index");

    if (staged.empty())
    {
        std::cout << "Nothing to commit (staging area is empty)\n";
        return;
    }

    std::string parent = get_head_commit();
    std::string timestamp = current_timestamp();

    // --- check for a pending merge (second parent) ---
    std::string parent2;
    bool is_merge = fs::exists(".my_git/MERGE_HEAD");
    if (is_merge)
    {
        parent2 = read_file(".my_git/MERGE_HEAD");
        while (!parent2.empty() && (parent2.back() == '\n' || parent2.back() == '\r'))
            parent2.pop_back();
    }

    // --- Build the full snapshot as a path -> content map (CHANGED for Phase 9: supports nested paths) ---
    std::map<std::string, std::string> snapshot;

    if (!parent.empty())
    {
        snapshot = reconstruct_commit(parent);
    }

    // Overlay staged files (add new, or update existing) — preserve full relative path
    for (const auto &filename : staged)
    {
        fs::path src = fs::path(".my_git/staging") / filename; // CHANGED: no .filename(), keep subdirectory structure
        std::string content = read_file(src);
        snapshot[filename] = content; // insert or overwrite
    }

    // CHANGED: build a (possibly nested) tree object from the snapshot map (Phase 9)
    std::string tree_hash = build_tree_from_map(snapshot);

    // --- Build hash input: metadata + all file contents ---
    std::string hash_input = "message: " + message + "\n" + "timestamp: " + timestamp + "\n" + "parent: " + parent + "\n" + "parent2: " + parent2 + "\n" + "tree: " + tree_hash + "\n";
    for (auto &[path, content] : snapshot)
    {
        hash_input += path + ":" + content + "\n";
    }

    std::string new_id = sha1(hash_input);

    // CHANGED: only create the commit directory for metadata — no files/ subfolder
    fs::path commit_dir = fs::path(".my_git/commits") / new_id;
    fs::create_directories(commit_dir);

    // Write metadata
    std::string metadata = "message: " + message + "\n" + "timestamp: " + timestamp + "\n" + "parent: " + parent + "\n" + "parent2: " + parent2 + "\n" + "tree: " + tree_hash + "\n";
    write_file(commit_dir / "metadata", metadata);

    // Update HEAD
    set_head_commit(new_id);

    // Clear staging area and index (CHANGED: preserve full path when removing)
    for (const auto &filename : staged)
    {
        fs::path staged_file = fs::path(".my_git/staging") / filename;
        fs::remove(staged_file);
        if (staged_file.has_parent_path())
            remove_empty_dirs_upward(staged_file.parent_path(), fs::path(".my_git/staging"));
    }
    write_file(".my_git/index", "");

    // Clear the pending merge marker
    if (is_merge)
    {
        fs::remove(".my_git/MERGE_HEAD");
        std::cout << "[merge commit " << new_id.substr(0, 7) << "] " << message << "\n";
    }
    else
    {
        std::cout << "[commit " << new_id.substr(0, 7) << "] " << message << "\n";
    }
}
