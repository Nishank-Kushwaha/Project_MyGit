#pragma once
#include <string>
#include <vector>

struct DiffLine
{
    char type; // ' ' = unchanged, '-' = removed, '+' = added
    std::string text;
};

std::vector<std::string> split_lines(const std::string &content);

std::vector<std::vector<int>> build_lcs_table(const std::vector<std::string> &a, const std::vector<std::string> &b);

std::vector<DiffLine> diff_lines(const std::vector<std::string> &a, const std::vector<std::string> &b);
