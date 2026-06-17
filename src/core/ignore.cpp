#include "core/ignore.h"
#include <fstream>
#include <filesystem>

std::vector<std::string> load_ignore_rules()
{
    std::vector<std::string> rules;
    std::ifstream in(".mygitignore");
    if (!in) return rules;

    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        rules.push_back(line);
    }
    return rules;
}

bool matches_ignore_rule(const std::string &relpath, const std::vector<std::string> &rules)
{
    if (relpath.rfind(".my_git/", 0) == 0 || relpath == ".my_git")
        return true;

    std::string filename = std::filesystem::path(relpath).filename().string();

    for (const auto &rule : rules)
    {
        if (rule.back() == '/')
        {
            std::string dir = rule.substr(0, rule.size() - 1);
            if (relpath.rfind(dir + "/", 0) == 0)
                return true;
        }
        else if (rule.size() > 2 && rule[0] == '*' && rule[1] == '.')
        {
            std::string ext = rule.substr(1);
            if (filename.size() >= ext.size() &&
                filename.substr(filename.size() - ext.size()) == ext)
                return true;
        }
        else
        {
            if (filename == rule || relpath == rule)
                return true;
        }
    }
    return false;
}
