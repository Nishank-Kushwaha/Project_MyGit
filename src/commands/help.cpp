#include "commands/help.h"
#include <iostream>
#include <iomanip>
#include <vector>

void cmd_help()
{
    std::cout << "my_git - a simplified version control system\n";
    std::cout << "Usage: my_git <command> [<args>]\n";
    std::cout << "These are the available my_git commands:\n\n";

    std::vector<std::pair<std::string, std::string>> all_commands = {
        {"init", "Create empty repository"},
        {"add", "Stage file for commit"},
        {"commit", "Save staged changes permanently"},
        {"log", "Show commit history (linear)"},
        {"status", "Show staged/modified/untracked files"},
        {"branch", "Create or list branches"},
        {"checkout", "Switch branch or commit"},
        {"diff", "Compare two commit snapshots"},
        {"merge", "Combine another branch's changes"},
        {"graph", "Display commit DAG with branches"},
        {"remote", "Add a remote repository"},
        {"push", "Send local commits to a remote"},
        {"fetch", "Download commits from a remote"},
        {"pull", "Fetch and merge from a remote"},
        {"clone", "Clone a repository into a new directory"},
        {"hash-object", "Compute and store a blob object"},
        {"cat-file", "Print contents of an object"},
        {"write-tree", "Build a tree object from directory"},
        {"ls-tree", "List entries of a tree object"},
        {"fsck", "Perform repository consistency checks"},
    };

    for (const auto &[name, desc] : all_commands)
    {
        std::cout << "  " << std::left << std::setw(15) << name << desc << "\n";
    }

    std::cout << "\nRun 'my_git <command>' with no arguments to see usage details (if applicable).\n";
}
