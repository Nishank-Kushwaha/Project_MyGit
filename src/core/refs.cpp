#include "core/refs.h"
#include "utils/file_io.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

std::string get_current_branch_ref()
{
    std::string head = trim_nl(read_file(".my_git/HEAD"));
    if (head.rfind("ref: ", 0) == 0)
        return head.substr(5);
    return "";
}

std::string get_head_commit()
{
    std::string head = trim_nl(read_file(".my_git/HEAD"));
    if (head.rfind("ref: ", 0) == 0)
    {
        std::string ref = head.substr(5);
        fs::path refpath = fs::path(".my_git") / ref;
        if (!fs::exists(refpath)) return "";
        return trim_nl(read_file(refpath));
    }
    return head;
}

void set_head_commit(const std::string &hash)
{
    std::string head = trim_nl(read_file(".my_git/HEAD"));
    if (head.rfind("ref: ", 0) == 0)
    {
        std::string ref = head.substr(5);
        fs::path refpath = fs::path(".my_git") / ref;
        fs::create_directories(refpath.parent_path());
        write_file(refpath, hash + "\n");
    }
    else
    {
        write_file(".my_git/HEAD", hash + "\n");
    }
}
