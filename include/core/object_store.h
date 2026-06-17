#pragma once
#include <string>
#include <map>
#include <filesystem>

std::string zlib_compress(const std::string &input);
std::string zlib_decompress(const std::string &compressed, size_t original_size);
std::string write_object(const std::string &content, const std::string &type);
std::string read_object(const std::string &hash);
std::string write_tree(const std::filesystem::path &dir_path);
std::map<std::string, std::string> load_tree_recursive(const std::string &tree_hash);
std::map<std::string, std::string> reconstruct_commit(const std::string &commit_hash);
std::string build_tree_from_map(const std::map<std::string, std::string> &files);
bool object_exists(const std::string &hash);
