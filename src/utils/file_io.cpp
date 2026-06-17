#include "utils/file_io.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

std::string trim_nl(std::string s)
{
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}

std::string read_file(const std::filesystem::path &p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return "";
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_file(const std::filesystem::path &p, const std::string &content)
{
    std::ofstream out(p, std::ios::binary);
    out << content;
}

std::vector<std::string> read_lines(const std::filesystem::path &p)
{
    std::vector<std::string> lines;
    std::ifstream in(p);
    std::string line;
    while (std::getline(in, line))
        if (!line.empty())
            lines.push_back(line);
    return lines;
}

bool files_equal(const std::filesystem::path &a, const std::filesystem::path &b)
{
    if (!std::filesystem::exists(a) || !std::filesystem::exists(b))
        return false;
    return read_file(a) == read_file(b);
}

void remove_empty_dirs_upward(std::filesystem::path dir, const std::filesystem::path &stop_at)
{
    while (!dir.empty() && dir != stop_at && dir != "." && fs::exists(dir) && fs::is_empty(dir))
    {
        fs::path parent = dir.parent_path();
        fs::remove(dir);
        if (parent == dir)
            break;
        dir = parent;
    }
}
