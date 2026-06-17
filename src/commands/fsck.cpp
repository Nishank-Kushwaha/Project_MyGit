#include "commands/fsck.h"
#include "core/object_store.h"
#include "core/refs.h"
#include "core/commits.h"
#include "core/diff_engine.h"
#include "utils/file_io.h"
#include "utils/sha1.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <set>
#include <map>
#include <vector>

namespace fs = std::filesystem;

static void check_tree_recursive(const std::string &tree_hash, int &errors)
{
    if (!object_exists(tree_hash))
    {
        std::cout << "[ERROR] missing tree object: " << tree_hash.substr(0, 7) << "\n";
        errors++;
        return;
    }
    std::string raw = read_object(tree_hash);
    size_t null_pos = raw.find('\0');
    std::istringstream iss(raw.substr(null_pos + 1));
    std::string line;
    while (std::getline(iss, line))
    {
        if (line.empty())
            continue;
        std::istringstream ls(line);
        std::string mode, type, hash, name;
        ls >> mode >> type >> hash;

        if (!object_exists(hash))
        {
            std::cout << "[ERROR] missing " << type << " object: " << hash.substr(0, 7)
                      << " (referenced by tree " << tree_hash.substr(0, 7) << ")\n";
            errors++;
            continue;
        }
        if (type == "tree")
            check_tree_recursive(hash, errors);
    }
}

static int run_fsck_checks()
{
    int errors = 0;

    for (const auto &entry : fs::directory_iterator(".my_git/commits"))
    {
        std::string hash = entry.path().filename().string();
        std::string metadata = read_file(entry.path() / "metadata");
        std::istringstream iss(metadata);
        std::string line, parent, parent2, tree;
        while (std::getline(iss, line))
        {
            if (line.rfind("parent2:", 0) == 0)
                parent2 = trim_nl(line.substr(9));
            else if (line.rfind("parent:", 0) == 0)
                parent = trim_nl(line.substr(8));
            else if (line.rfind("tree:", 0) == 0)
                tree = trim_nl(line.substr(6));
        }

        if (tree.empty())
        {
            std::cout << "[ERROR] commit " << hash.substr(0, 7) << " has no tree: field\n";
            errors++;
        }
        else
        {
            check_tree_recursive(tree, errors);
        }

        if (!parent.empty() && !fs::exists(fs::path(".my_git/commits") / parent / "metadata"))
        {
            std::cout << "[ERROR] commit " << hash.substr(0, 7) << " has dangling parent: " << parent.substr(0, 7) << "\n";
            errors++;
        }
        if (!parent2.empty() && !fs::exists(fs::path(".my_git/commits") / parent2 / "metadata"))
        {
            std::cout << "[ERROR] commit " << hash.substr(0, 7) << " has dangling parent2: " << parent2.substr(0, 7) << "\n";
            errors++;
        }
    }

    for (const auto &entry : fs::recursive_directory_iterator(".my_git/refs"))
    {
        if (!entry.is_regular_file())
            continue;
        std::string hash = trim_nl(read_file(entry.path()));
        if (hash.empty())
            continue;
        if (!fs::exists(fs::path(".my_git/commits") / hash / "metadata"))
        {
            std::cout << "[ERROR] ref " << entry.path().string() << " points to missing commit "
                      << hash.substr(0, 7) << "\n";
            errors++;
        }
    }
    return errors;
}

void cmd_fsck()
{
    int errors = run_fsck_checks();
    if (errors == 0)
        std::cout << "fsck: repository is healthy (0 errors)\n";
    else
        std::cout << "\nfsck: " << errors << " error(s) found\n";
}

void cmd_selftest()
{
    int passed = 0, total = 0;

    // 1. SHA-1 correctness
    total++;
    std::string sha1_result = sha1("hello");
    std::string sha1_expected = "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d";
    bool sha1_ok = (sha1_result == sha1_expected);
    if (sha1_ok)
        passed++;
    std::cout << (sha1_ok ? "[PASS] " : "[FAIL] ")
              << "SHA-1(\"hello\") = " << sha1_result << "\n";

    // 2. LCS correctness
    total++;
    std::vector<std::string> a = {"Hello World", "Line2", "Line3"};
    std::vector<std::string> b = {"Hello Git", "Line2", "Line3"};
    auto dp = build_lcs_table(a, b);
    int lcs_len = dp[dp.size() - 1][dp[0].size() - 1];
    bool lcs_ok = (lcs_len == 2);
    if (lcs_ok)
        passed++;
    std::cout << (lcs_ok ? "[PASS] " : "[FAIL] ")
              << "LCS length = " << lcs_len << " (expected 2)\n";

    // 3. Object store round-trip (write -> read -> decompress)
    total++;
    std::string test_content = "Phase 7 object store self-test\n";
    std::string obj_hash = write_object(test_content, "blob");
    std::string raw = read_object(obj_hash);
    size_t null_pos = raw.find('\0');
    std::string recovered = (null_pos != std::string::npos) ? raw.substr(null_pos + 1) : "";
    bool obj_ok = (recovered == test_content);
    if (obj_ok)
        passed++;
    std::cout << (obj_ok ? "[PASS] " : "[FAIL] ")
              << "Object store round-trip (write_object -> read_object)\n";

    // 4. Blob hash matches Git's "blob <size>\0<content>" formula
    total++;
    std::string expected_blob_hash = sha1("blob " + std::to_string(test_content.size()) + '\0' + test_content);
    bool blob_hash_ok = (obj_hash == expected_blob_hash);
    if (blob_hash_ok)
        passed++;
    std::cout << (blob_hash_ok ? "[PASS] " : "[FAIL] ")
              << "Blob hash matches sha1(\"blob <size>\\0<content>\")\n";

    // 5. Merge-base detection (only if main/feature both exist)
    if (fs::exists(".my_git/refs/main") && fs::exists(".my_git/refs/feature"))
    {
        total++;
        std::string base = find_merge_base(read_file(".my_git/refs/main"), read_file(".my_git/refs/feature"));
        bool mb_ok = !base.empty();
        if (mb_ok)
            passed++;
        std::cout << (mb_ok ? "[PASS] " : "[FAIL] ")
                  << "Merge base = " << (mb_ok ? base.substr(0, 7) : "(none found)") << "\n";
    }

    // 6. Object-database traversal
    total++;

    bool traversal_ok = true;

    for (const auto &entry : fs::directory_iterator(".my_git/commits"))
    {
        std::string hash = entry.path().filename().string();

        auto snapshot = reconstruct_commit(hash);

        if (snapshot.empty())
        {
            traversal_ok = false;
            break;
        }
    }

    if (traversal_ok)
        passed++;

    std::cout << (traversal_ok ? "[PASS] " : "[FAIL] ")
              << "Commit -> Tree -> Blob traversal\n";

    std::cout << "\n"
              << passed << "/" << total << " checks passed\n";

    // 7. Recursive tree builder from a flat path→content map
    std::map<std::string, std::string> test_files = {
        {"file1.txt", "root file"},
        {"src/main.cpp", "int main(){}"},
        {"src/utils/helper.cpp", "helper code"}};
    std::string root_tree = build_tree_from_map(test_files);
    std::cout << "Root tree: " << root_tree << "\n";
}
