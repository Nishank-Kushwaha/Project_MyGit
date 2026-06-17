#include "commands/remote.h"
#include "commands/merge.h"
#include "core/refs.h"
#include "core/commits.h"
#include "core/object_store.h"
#include "core/remote.h"
#include "utils/file_io.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <set>
#include <map>
#include <vector>

namespace fs = std::filesystem;

void cmd_remote_add(const std::string &name, const std::string &path)
{
    std::string config = read_file(".my_git/config");
    std::string abs_path = fs::absolute(fs::path(path)).string();
    std::replace(abs_path.begin(), abs_path.end(), '\\', '/');
    config += "remote." + name + ".url=" + abs_path + "\n";
    write_file(".my_git/config", config);
    std::cout << "Added remote '" << name << "' -> " << abs_path << "\n";
}

void cmd_push(const std::string &remote_name, const std::string &branch_name)
{
    std::string remote_path = get_remote_url(remote_name);
    if (remote_path.empty())
    {
        std::cout << "Error: remote '" << remote_name << "' not found\n";
        return;
    }

    fs::path remote_root = remote_path;
    if (!fs::exists(remote_root / ".my_git"))
    {
        std::cout << "Error: '" << remote_path << "' is not a my_git repository\n";
        return;
    }

    fs::path local_ref = fs::path(".my_git/refs") / branch_name;
    if (!fs::exists(local_ref))
    {
        std::cout << "Error: branch '" << branch_name << "' does not exist locally\n";
        return;
    }

    std::string local_hash = read_file(local_ref);
    fs::path remote_ref = remote_root / ".my_git/refs" / branch_name;

    // Fast-forward check
    if (fs::exists(remote_ref))
    {
        std::string remote_hash = read_file(remote_ref);

        if (!remote_hash.empty())
        { // <-- NEW: only check if remote actually has a commit
            if (remote_hash == local_hash)
            {
                std::cout << "Already up to date.\n";
                return;
            }
            std::set<std::string> local_ancestors = get_ancestors(local_hash);
            if (!local_ancestors.count(remote_hash))
            {
                std::cout << "Error: push rejected (non-fast-forward). Remote has commits you don't have.\n";
                return;
            }
        }
    }

    // Copy missing commit objects
    int copied = copy_commits_recursive(".", remote_root, local_hash);

    // Update remote's ref
    write_file(remote_ref, local_hash);

    std::cout << "Pushed '" << branch_name << "' to '" << remote_name << "' ("
              << copied << " new commit object(s))\n";
}

void cmd_fetch(const std::string &remote_name)
{
    std::string remote_path = get_remote_url(remote_name);
    if (remote_path.empty())
    {
        std::cout << "Error: remote '" << remote_name << "' not found\n";
        return;
    }

    fs::path remote_root = remote_path;
    if (!fs::exists(remote_root / ".my_git"))
    {
        std::cout << "Error: '" << remote_path << "' is not a my_git repository\n";
        return;
    }

    fs::path remote_refs_dir = remote_root / ".my_git/refs";
    fs::path local_tracking_dir = fs::path(".my_git/refs/remotes") / remote_name;
    fs::create_directories(local_tracking_dir);

    int total_copied = 0;
    int branches_updated = 0;

    for (const auto &entry : fs::directory_iterator(remote_refs_dir))
    {
        if (!entry.is_regular_file())
            continue; // skip "remotes" subfolder if present

        std::string branch_name = entry.path().filename().string();
        std::string remote_hash = read_file(entry.path());
        if (remote_hash.empty())
            continue; // empty branch, nothing to fetch

        // Copy missing commit objects FROM remote TO local
        total_copied += copy_commits_recursive(remote_root, ".", remote_hash);

        // Update local remote-tracking ref
        fs::path tracking_ref = local_tracking_dir / branch_name;
        std::string old_hash = fs::exists(tracking_ref) ? read_file(tracking_ref) : "";
        if (old_hash != remote_hash)
        {
            write_file(tracking_ref, remote_hash);
            branches_updated++;
            std::cout << "  " << remote_name << "/" << branch_name
                      << "  " << (old_hash.empty() ? "(new)" : old_hash.substr(0, 7))
                      << " -> " << remote_hash.substr(0, 7) << "\n";
        }
    }

    std::cout << "Fetched from '" << remote_name << "': " << branches_updated
              << " branch(es) updated, " << total_copied << " new commit object(s)\n";
}

void cmd_pull(const std::string &remote_name, const std::string &branch_name)
{
    std::cout << "Pulling from '" << remote_name << "'...\n";
    cmd_fetch(remote_name);

    std::string tracking_ref = "remotes/" + remote_name + "/" + branch_name;

    if (!fs::exists(fs::path(".my_git/refs") / tracking_ref))
    {
        std::cout << "Error: nothing to merge (no tracking ref for '" << tracking_ref << "')\n";
        return;
    }

    std::cout << "\nMerging " << tracking_ref << " into current branch...\n";
    cmd_merge(tracking_ref);
}

void cmd_clone(const std::string &source, const std::string &destination)
{
    fs::path src(source);
    fs::path src_git = src / ".my_git";
    fs::path dst(destination);
    fs::path dst_git = dst / ".my_git";

    // Milestone 2: Validate source
    if (!fs::exists(src) || !fs::is_directory(src))
    {
        std::cout << "fatal: source path '" << source << "' does not exist.\n";
        return;
    }
    if (!fs::exists(src_git) || !fs::is_directory(src_git))
    {
        std::cout << "fatal: '" << source << "' is not a my_git repository.\n";
        return;
    }

    // Milestone 3: Create destination
    if (fs::exists(dst))
    {
        std::cout << "fatal: destination '" << destination << "' already exists.\n";
        return;
    }
    fs::create_directories(dst_git);
    std::cout << "Cloning into '" << destination << "'...\n";

    // Milestone 4: Copy repository database
    // Copy directories recursively
    for (const std::string &entry : {"objects", "commits", "refs", "staging"})
    {
        fs::path s = src_git / entry;
        fs::path d = dst_git / entry;
        if (fs::exists(s))
            fs::copy(s, d, fs::copy_options::recursive);
        else
            fs::create_directories(d); // create empty if missing in source
    }

    // Copy individual files
    for (const std::string &f : {"HEAD", "index"})
    {
        fs::path s = src_git / f;
        fs::path d = dst_git / f;
        if (fs::exists(s))
            fs::copy_file(s, d);
        else
            write_file(d, "");
    }

    // Milestone 5a: Create origin remote (absolute path)
    fs::path abs_src = fs::absolute(src);
    std::string abs_src_str = abs_src.string();
    // Normalize to forward slashes for consistency
    std::replace(abs_src_str.begin(), abs_src_str.end(), '\\', '/');
    write_file(dst_git / "config", "remote.origin.url=" + abs_src_str + "\n");

    // Milestone 5b: Initialize remote-tracking refs
    // so that 'fetch origin' after cloning shows correct deltas
    fs::path tracking_dir = dst_git / "refs" / "remotes" / "origin";
    fs::create_directories(tracking_dir);

    fs::path src_refs = src_git / "refs";
    for (const auto &entry : fs::directory_iterator(src_refs))
    {
        if (!entry.is_regular_file())
            continue;
        std::string branch = entry.path().filename().string();
        std::string hash = trim_nl(read_file(entry.path()));
        if (!hash.empty())
            write_file(tracking_dir / branch, hash);
    }

    // Milestone 6: Determine default branch from source HEAD
    std::string head_content = read_file(src_git / "HEAD");
    while (!head_content.empty() && (head_content.back() == '\n' || head_content.back() == '\r' || head_content.back() == ' '))
        head_content.pop_back();

    std::string default_branch = "main"; // fallback
    std::string head_ref = "";

    if (head_content.rfind("ref: ", 0) == 0)
    {
        head_ref = head_content.substr(5); // "refs/main"
        size_t slash = head_ref.rfind('/');
        default_branch = (slash != std::string::npos) ? head_ref.substr(slash + 1) : head_ref;
    }
    else
    {
        // detached HEAD — head_content is a raw commit hash
        head_ref = head_content;
        default_branch = "";
    }

    std::cout << "Default branch: " << (default_branch.empty() ? "(detached HEAD)" : default_branch) << "\n";

    // Milestone 7: Checkout working tree
    std::string head_commit;

    if (!head_ref.empty() && fs::exists(dst_git / head_ref))
    {
        head_commit = trim_nl(read_file(dst_git / head_ref));
    }
    else if (head_content.rfind("ref: ", 0) != 0)
    {
        head_commit = head_content; // detached HEAD
    }

    if (head_commit.empty())
    {
        std::cout << "warning: HEAD has no commits yet. Empty repository cloned.\n";
        return;
    }

    // Reconstruct snapshot from the CLONED repo's object database
    // We temporarily switch CWD so read_object/write_file resolve
    // relative to the clone root (where .my_git/ now lives)
    fs::path original_cwd = fs::current_path();
    fs::current_path(dst);

    auto snapshot = reconstruct_commit(head_commit);
    for (const auto &[path, content] : snapshot)
    {
        fs::path full = fs::path(".") / path;
        fs::create_directories(full.parent_path());
        write_file(full, content);
    }

    fs::current_path(original_cwd);

    std::cout << "Done. " << snapshot.size() << " file(s) checked out.\n";
}
