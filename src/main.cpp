#include "commands/init.h"
#include "commands/add.h"
#include "commands/commit.h"
#include "commands/log.h"
#include "commands/status.h"
#include "commands/diff.h"
#include "commands/branch.h"
#include "commands/checkout.h"
#include "commands/merge.h"
#include "commands/graph.h"
#include "commands/remote.h"
#include "commands/fsck.h"
#include "commands/plumbing.h"
#include "commands/help.h"
#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        cmd_help();
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "init")
    {
        cmd_init();
    }
    else if (cmd == "help")
    {
        cmd_help();
    }
    else if (cmd == "add")
    {
        if (argc < 3)
        {
            std::cout << "Usage: my_git add <filename>\n";
            return 1;
        }
        cmd_add(argv[2]);
    }
    else if (cmd == "commit")
    {
        if (argc < 3)
        {
            std::cout << "Usage: my_git commit \"<message>\"\n";
            return 1;
        }
        cmd_commit(argv[2]);
    }
    else if (cmd == "log")
    {
        cmd_log();
    }
    else if (cmd == "status")
    {
        cmd_status();
    }
    else if (cmd == "branch")
    {
        if (argc < 3)
            cmd_branch_list();
        else
            cmd_branch(argv[2]);
    }
    else if (cmd == "checkout")
    {
        if (argc < 3)
        {
            std::cout << "Usage: my_git checkout <name>\n";
            return 1;
        }
        cmd_checkout(argv[2]);
    }
    else if (cmd == "diff")
    {
        if (argc < 4)
        {
            std::cout << "Usage: my_git diff <hash1> <hash2>\n";
            return 1;
        }
        cmd_diff(argv[2], argv[3]);
    }
    else if (cmd == "merge")
    {
        if (argc < 3)
        {
            std::cout << "Usage: my_git merge <branch>\n";
            return 1;
        }
        cmd_merge(argv[2]);
    }
    else if (cmd == "graph")
    {
        cmd_graph();
    }
    else if (cmd == "remote")
    {
        if (argc < 5 || std::string(argv[2]) != "add")
        {
            std::cout << "Usage: my_git remote add <name> <path>\n";
            return 1;
        }
        cmd_remote_add(argv[3], argv[4]);
    }
    else if (cmd == "push")
    {
        if (argc < 4)
        {
            std::cout << "Usage: my_git push <remote> <branch>\n";
            return 1;
        }
        cmd_push(argv[2], argv[3]);
    }
    else if (cmd == "fetch")
    {
        if (argc < 3)
        {
            std::cout << "Usage: my_git fetch <remote>\n";
            return 1;
        }
        cmd_fetch(argv[2]);
    }
    else if (cmd == "pull")
    {
        if (argc < 4)
        {
            std::cout << "Usage: my_git pull <remote> <branch>\n";
            return 1;
        }
        cmd_pull(argv[2], argv[3]);
    }
    else if (cmd == "clone")
    {
        if (argc < 4)
        {
            std::cout << "Usage: my_git clone <source> <destination>\n";
            return 1;
        }
        cmd_clone(argv[2], argv[3]);
    }
    else if (cmd == "hash-object")
    {
        if (argc < 3)
        {
            std::cout << "Usage: my_git hash-object <file>\n";
            return 1;
        }
        cmd_hash_object(argv[2]);
    }
    else if (cmd == "cat-file")
    {
        if (argc < 4 || std::string(argv[2]) != "-p")
        {
            std::cout << "Usage: my_git cat-file -p <hash>\n";
            return 1;
        }
        cmd_cat_file(argv[3]);
    }
    else if (cmd == "write-tree")
    {
        cmd_write_tree();
    }
    else if (cmd == "ls-tree")
    {
        if (argc < 3)
        {
            std::cout << "Usage: my_git ls-tree <hash>\n";
            return 1;
        }
        cmd_ls_tree(argv[2]);
    }
    else if (cmd == "fsck")
    {
        cmd_fsck();
    }
    else if (cmd == "selftest")
    {
        cmd_selftest();
    }
    else
    {
        std::cout << "Unknown command: " << cmd << "\n";
        return 1;
    }

    return 0;
}
