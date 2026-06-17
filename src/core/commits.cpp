#include "core/commits.h"
#include "utils/file_io.h"
#include <queue>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

std::set<std::string> get_ancestors(const std::string &commit_hash)
{
    std::set<std::string> ancestors;
    std::queue<std::string> q;
    if (!commit_hash.empty()) q.push(commit_hash);

    while (!q.empty())
    {
        std::string current = q.front(); q.pop();
        if (current.empty() || ancestors.count(current)) continue;
        ancestors.insert(current);

        std::string metadata = read_file(fs::path(".my_git/commits") / current / "metadata");
        std::istringstream iss(metadata);
        std::string line, parent, parent2;
        while (std::getline(iss, line))
        {
            if (line.rfind("parent2:", 0) == 0) parent2 = trim_nl(line.substr(9));
            else if (line.rfind("parent:", 0) == 0) parent = trim_nl(line.substr(8));
        }
        if (!parent.empty())  q.push(parent);
        if (!parent2.empty()) q.push(parent2);
    }
    return ancestors;
}

std::string find_merge_base(const std::string &ours, const std::string &theirs)
{
    std::set<std::string> ours_ancestors = get_ancestors(ours);
    std::queue<std::string> q;
    std::set<std::string> visited;
    if (!theirs.empty()) { q.push(theirs); visited.insert(theirs); }

    while (!q.empty())
    {
        std::string current = q.front(); q.pop();
        if (ours_ancestors.count(current)) return current;

        std::string metadata = read_file(fs::path(".my_git/commits") / current / "metadata");
        std::istringstream iss(metadata);
        std::string line, parent, parent2;
        while (std::getline(iss, line))
        {
            if (line.rfind("parent2:", 0) == 0) parent2 = trim_nl(line.substr(9));
            else if (line.rfind("parent:", 0) == 0) parent = trim_nl(line.substr(8));
        }
        if (!parent.empty() && !visited.count(parent))   { visited.insert(parent);  q.push(parent);  }
        if (!parent2.empty() && !visited.count(parent2)) { visited.insert(parent2); q.push(parent2); }
    }
    return "";
}

std::string get_file_at_commit(const std::string &commit_hash, const std::string &filename)
{
    if (commit_hash.empty()) return "";
    fs::path p = fs::path(".my_git/commits") / commit_hash / "files" / filename;
    return fs::exists(p) ? read_file(p) : "";
}

int copy_commits_recursive(const fs::path &src_root, const fs::path &dst_root, const std::string &start_hash)
{
    int copied = 0;
    std::string current = start_hash;

    while (!current.empty())
    {
        fs::path src_commit = src_root / ".my_git/commits" / current;
        fs::path dst_commit = dst_root / ".my_git/commits" / current;

        fs::path src_objects = src_root / ".my_git/objects";
        fs::path dst_objects = dst_root / ".my_git/objects";
        if (fs::exists(src_objects))
        {
            for (const auto &aa : fs::directory_iterator(src_objects))
            {
                if (!aa.is_directory()) continue;
                for (const auto &obj : fs::directory_iterator(aa))
                {
                    fs::path dst_obj = dst_objects / aa.path().filename() / obj.path().filename();
                    if (!fs::exists(dst_obj))
                    {
                        fs::create_directories(dst_obj.parent_path());
                        fs::copy_file(obj.path(), dst_obj);
                    }
                }
            }
        }

        if (fs::exists(dst_commit)) break;

        fs::create_directories(dst_commit);
        fs::copy(src_commit, dst_commit,
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        copied++;

        std::string metadata = read_file(src_commit / "metadata");
        std::istringstream iss(metadata);
        std::string line, parent, parent2;
        while (std::getline(iss, line))
        {
            if (line.rfind("parent2:", 0) == 0) parent2 = trim_nl(line.substr(9));
            else if (line.rfind("parent:", 0) == 0) parent = trim_nl(line.substr(8));
        }
        if (!parent2.empty()) copied += copy_commits_recursive(src_root, dst_root, parent2);
        current = parent;
    }
    return copied;
}
