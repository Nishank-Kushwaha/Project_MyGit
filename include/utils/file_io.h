#pragma once
#include <string>
#include <vector>
#include <filesystem>

std::string trim_nl(std::string s);
std::string read_file(const std::filesystem::path &p);
void write_file(const std::filesystem::path &p, const std::string &content);
std::vector<std::string> read_lines(const std::filesystem::path &p);
bool files_equal(const std::filesystem::path &a, const std::filesystem::path &b);
void remove_empty_dirs_upward(std::filesystem::path dir, const std::filesystem::path &stop_at = ".");
