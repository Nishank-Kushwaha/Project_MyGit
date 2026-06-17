#pragma once
#include <string>
#include <set>
#include <filesystem>

std::set<std::string> get_ancestors(const std::string &commit_hash);

std::string find_merge_base(const std::string &ours, const std::string &theirs);

std::string get_file_at_commit(const std::string &commit_hash, const std::string &filename);

int copy_commits_recursive(const std::filesystem::path &src_root, const std::filesystem::path &dst_root, const std::string &start_hash);
