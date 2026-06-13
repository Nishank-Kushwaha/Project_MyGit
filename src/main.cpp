#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <vector>
#include <chrono>
#include <ctime>
#include <sstream>
#include <set>

namespace fs = std::filesystem;

struct DiffLine
{
    char type; // ' ' = unchanged, '-' = removed, '+' = added
    std::string text;
};

// Read entire file content as a string
std::string read_file(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return "";
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Write a string to a file (overwrites)
void write_file(const fs::path &p, const std::string &content)
{
    std::ofstream out(p, std::ios::binary);
    out << content;
}

// Read each line of a file into a vector (used for index)
std::vector<std::string> read_lines(const fs::path &p)
{
    std::vector<std::string> lines;
    std::ifstream in(p);
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty())
            lines.push_back(line);
    }
    return lines;
}

// Get current timestamp as a readable string
std::string current_timestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::string s = std::ctime(&t);
    if (!s.empty() && s.back() == '\n')
        s.pop_back(); // remove trailing newline
    return s;
}

// Compare both files and return weather both are equal or not
bool files_equal(const fs::path &a, const fs::path &b)
{
    if (!fs::exists(a) || !fs::exists(b))
        return false;
    return read_file(a) == read_file(b);
}

// SHA-1 implementation
std::string sha1(const std::string &input)
{
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

    std::vector<uint8_t> msg(input.begin(), input.end());
    uint64_t ml = msg.size() * 8;

    msg.push_back(0x80);
    while (msg.size() % 64 != 56)
        msg.push_back(0x00);
    for (int i = 7; i >= 0; --i)
        msg.push_back((ml >> (i * 8)) & 0xFF);

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64)
    {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
        {
            w[i] = (uint32_t(msg[chunk + i * 4]) << 24) | (uint32_t(msg[chunk + i * 4 + 1]) << 16) | (uint32_t(msg[chunk + i * 4 + 2]) << 8) | (uint32_t(msg[chunk + i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i)
        {
            uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = (v << 1) | (v >> 31);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

        for (int i = 0; i < 80; ++i)
        {
            uint32_t f, k;
            if (i < 20)
            {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    char buf[41];
    snprintf(buf, sizeof(buf), "%08x%08x%08x%08x%08x", h0, h1, h2, h3, h4);
    return std::string(buf);
}

// Returns "refs/main", "refs/feature", etc. if HEAD is symbolic; "" if detached
std::string get_current_branch_ref()
{
    std::string head_content = read_file(".my_git/HEAD");
    if (head_content.rfind("ref: ", 0) == 0)
    {
        std::string ref = head_content.substr(5);
        // trim trailing newline/whitespace
        while (!ref.empty() && (ref.back() == '\n' || ref.back() == '\r'))
            ref.pop_back();
        return ref;
    }
    return ""; // detached HEAD
}

// Resolves HEAD all the way down to a commit hash
std::string get_head_commit()
{
    std::string ref = get_current_branch_ref();
    if (!ref.empty())
    {
        return read_file(fs::path(".my_git") / ref); // e.g. .my_git/refs/main
    }
    // Detached: HEAD contains the commit hash directly
    std::string h = read_file(".my_git/HEAD");
    while (!h.empty() && (h.back() == '\n' || h.back() == '\r'))
        h.pop_back();
    return h;
}

// Updates wherever HEAD currently points (branch ref, or HEAD itself if detached)
void set_head_commit(const std::string &commit_hash)
{
    std::string ref = get_current_branch_ref();
    if (!ref.empty())
    {
        write_file(fs::path(".my_git") / ref, commit_hash);
    }
    else
    {
        write_file(".my_git/HEAD", commit_hash);
    }
}

// Split file content into lines
std::vector<std::string> split_lines(const std::string &content)
{
    std::vector<std::string> lines;
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line))
    {
        lines.push_back(line);
    }
    return lines;
}

// Build the LCS table
std::vector<std::vector<int>> build_lcs_table(const std::vector<std::string> &a, const std::vector<std::string> &b)
{
    int n = a.size(), m = b.size();
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));

    for (int i = 1; i <= n; i++)
    {
        for (int j = 1; j <= m; j++)
        {
            if (a[i - 1] == b[j - 1])
                dp[i][j] = dp[i - 1][j - 1] + 1;
            else
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
        }
    }
    return dp;
}

// Get the differentiating lines
std::vector<DiffLine> diff_lines(const std::vector<std::string> &a, const std::vector<std::string> &b)
{
    auto dp = build_lcs_table(a, b);
    std::vector<DiffLine> result;

    int n = a.size(), m = b.size();
    int i = n, j = m;

    while (i > 0 && j > 0)
    {
        if (a[i - 1] == b[j - 1])
        {
            result.push_back({' ', a[i - 1]});
            i--;
            j--;
        }
        else if (dp[i - 1][j] > dp[i][j - 1])
        {
            result.push_back({'-', a[i - 1]});
            i--;
        }
        else
        {
            result.push_back({'+', b[j - 1]});
            j--;
        }
    }
    while (i > 0)
    {
        result.push_back({'-', a[i - 1]});
        i--;
    }
    while (j > 0)
    {
        result.push_back({'+', b[j - 1]});
        j--;
    }

    std::reverse(result.begin(), result.end());
    return result;
}

// Find the ancestor's list
std::set<std::string> get_ancestors(const std::string &commit_hash)
{
    std::set<std::string> ancestors;
    std::string current = commit_hash;

    while (!current.empty())
    {
        ancestors.insert(current);
        std::string metadata = read_file(fs::path(".my_git/commits") / current / "metadata");
        std::istringstream iss(metadata);
        std::string line, parent;
        while (std::getline(iss, line))
        {
            if (line.rfind("parent:", 0) == 0)
                parent = line.substr(8);
        }
        current = parent;
    }
    return ancestors;
}

// Find the Least Comman Ancestor
std::string find_merge_base(const std::string &ours, const std::string &theirs)
{
    std::set<std::string> ours_ancestors = get_ancestors(ours);

    std::string current = theirs;
    while (!current.empty())
    {
        if (ours_ancestors.count(current))
            return current;

        std::string metadata = read_file(fs::path(".my_git/commits") / current / "metadata");
        std::istringstream iss(metadata);
        std::string line, parent;
        while (std::getline(iss, line))
        {
            if (line.rfind("parent:", 0) == 0)
                parent = line.substr(8);
        }
        current = parent;
    }
    return "";
}

// Returns content of a file in a given commit's snapshot ("" if file doesn't exist there)
std::string get_file_at_commit(const std::string &commit_hash, const std::string &filename)
{
    if (commit_hash.empty())
        return "";
    fs::path p = fs::path(".my_git/commits") / commit_hash / "files" / filename;
    return fs::exists(p) ? read_file(p) : "";
}

// Initialize my_git
void cmd_init()
{
    fs::create_directories(".my_git/commits");
    fs::create_directories(".my_git/staging");
    fs::create_directories(".my_git/refs");
    write_file(".my_git/HEAD", "ref: refs/main");
    if (!fs::exists(".my_git/refs/main"))
        write_file(".my_git/refs/main", "");
    write_file(".my_git/index", "");
    std::cout << "Initialized empty my_git repository\n";
}

// Visualisation of current 'DAG'
void cmd_graph()
{
    for (const auto &entry : fs::directory_iterator(".my_git/commits"))
    {
        std::string hash = entry.path().filename().string();
        std::string metadata = read_file(entry.path() / "metadata");
        std::istringstream iss(metadata);
        std::string line, parent, parent2;
        while (std::getline(iss, line))
        {
            if (line.rfind("parent2:", 0) == 0)
                parent2 = line.substr(9);
            else if (line.rfind("parent:", 0) == 0)
                parent = line.substr(8);
        }

        std::string p2_label = parent2.empty() ? "" : ("  parent2=" + parent2.substr(0, 7));
        std::cout << "* " << hash.substr(0, 7) << "  parent="
                  << (parent.empty() ? "-" : parent.substr(0, 7)) << p2_label << "\n";
    }

    std::cout << "\nBranches:\n";
    for (const auto &entry : fs::directory_iterator(".my_git/refs"))
    {
        std::string branch = entry.path().filename().string();
        std::string h = read_file(entry.path());
        std::cout << "  " << branch << " -> " << h.substr(0, 7) << "\n";
    }
}

void cmd_add(const std::string &filename)
{
    fs::path source = filename;

    // Check the file actually exists in the working directory
    if (!fs::exists(source))
    {
        std::cout << "Error: file '" << filename << "' does not exist\n";
        return;
    }

    // Copy the file into the staging area
    fs::path dest = fs::path(".my_git/staging") / source.filename();
    fs::copy_file(source, dest, fs::copy_options::overwrite_existing);

    // Add filename to index (if not already present)
    auto staged = read_lines(".my_git/index");
    bool already_staged = false;
    for (const auto &f : staged)
    {
        if (f == filename)
        {
            already_staged = true;
            break;
        }
    }

    if (!already_staged)
    {
        std::ofstream out(".my_git/index", std::ios::app); // append mode
        out << filename << "\n";
    }

    std::cout << "Staged '" << filename << "'\n";
}

void cmd_commit(const std::string &message)
{
    auto staged = read_lines(".my_git/index");

    if (staged.empty())
    {
        std::cout << "Nothing to commit (staging area is empty)\n";
        return;
    }

    std::string parent = get_head_commit();
    std::string timestamp = current_timestamp();

    // --- NEW: check for a pending merge (second parent) ---
    std::string parent2;
    bool is_merge = fs::exists(".my_git/MERGE_HEAD");
    if (is_merge)
    {
        parent2 = read_file(".my_git/MERGE_HEAD");
        while (!parent2.empty() && (parent2.back() == '\n' || parent2.back() == '\r'))
            parent2.pop_back();
    }

    // --- Build the full snapshot content first (in memory) ---
    std::vector<std::string> all_files;
    std::vector<std::string> all_contents;

    if (!parent.empty())
    {
        fs::path parent_files = fs::path(".my_git/commits") / parent / "files";
        for (const auto &entry : fs::directory_iterator(parent_files))
        {
            all_files.push_back(entry.path().filename().string());
            all_contents.push_back(read_file(entry.path()));
        }
    }

    // Overlay staged files (add new, or update existing)
    for (const auto &filename : staged)
    {
        fs::path src = fs::path(".my_git/staging") / fs::path(filename).filename();
        std::string content = read_file(src);
        std::string fname = fs::path(filename).filename().string();

        bool found = false;
        for (size_t i = 0; i < all_files.size(); ++i)
        {
            if (all_files[i] == fname)
            {
                all_contents[i] = content;
                found = true;
                break;
            }
        }
        if (!found)
        {
            all_files.push_back(fname);
            all_contents.push_back(content);
        }
    }

    // --- Build hash input: metadata + all file contents ---
    std::string hash_input = "message: " + message + "\n" + "timestamp: " + timestamp + "\n" + "parent: " + parent + "\n" + "parent2: " + parent2 + "\n"; // NEW
    for (size_t i = 0; i < all_files.size(); ++i)
    {
        hash_input += all_files[i] + ":" + all_contents[i] + "\n";
    }

    std::string new_id = sha1(hash_input);

    // --- Create commit folder and write the snapshot ---
    fs::path commit_dir = fs::path(".my_git/commits") / new_id;
    fs::create_directories(commit_dir / "files");

    for (size_t i = 0; i < all_files.size(); ++i)
    {
        write_file(commit_dir / "files" / all_files[i], all_contents[i]);
    }

    // Write metadata
    std::string metadata = "message: " + message + "\n" + "timestamp: " + timestamp + "\n" + "parent: " + parent + "\n" + "parent2: " + parent2 + "\n"; // NEW
    write_file(commit_dir / "metadata", metadata);

    // Update HEAD
    set_head_commit(new_id);

    // Clear staging area and index
    for (const auto &filename : staged)
    {
        fs::path staged_file = fs::path(".my_git/staging") / fs::path(filename).filename();
        fs::remove(staged_file);
    }
    write_file(".my_git/index", "");

    // NEW: clear the pending merge marker
    if (is_merge)
    {
        fs::remove(".my_git/MERGE_HEAD");
        std::cout << "[merge commit " << new_id.substr(0, 7) << "] " << message << "\n";
    }
    else
    {
        std::cout << "[commit " << new_id.substr(0, 7) << "] " << message << "\n";
    }
}

void cmd_log()
{
    std::string current = get_head_commit();

    if (current.empty())
    {
        std::cout << "No commits yet\n";
        return;
    }

    while (!current.empty())
    {
        fs::path metadata_path = fs::path(".my_git/commits") / current / "metadata";
        std::string content = read_file(metadata_path);

        std::cout << "commit " << current << "\n";

        // Parse metadata line by line
        std::istringstream iss(content);
        std::string line, parent;
        while (std::getline(iss, line))
        {
            if (line.rfind("message:", 0) == 0)
                std::cout << "    " << line.substr(9) << "\n"; // skip "message: "
            else if (line.rfind("timestamp:", 0) == 0)
                std::cout << "Date:   " << line.substr(11) << "\n";
            else if (line.rfind("parent:", 0) == 0)
                parent = line.substr(8); // skip "parent: "
        }
        std::cout << "\n";

        current = parent; // move to parent commit (could be empty -> loop ends)
    }
}

void cmd_status()
{
    auto staged = read_lines(".my_git/index");
    std::string head = get_head_commit();

    // --- Section 1: Staged files ---
    std::cout << "Staged for commit:\n";
    if (staged.empty())
    {
        std::cout << "  (none)\n";
    }
    else
    {
        for (const auto &f : staged)
        {
            std::cout << "  " << f << "\n";
        }
    }

    // --- Section 2: Modified files (not staged) ---
    std::cout << "\nModified (Changes not staged for commit):\n";
    bool any_modified = false;

    for (const auto &entry : fs::directory_iterator("."))
    {
        if (!entry.is_regular_file())
            continue;

        std::string filename = entry.path().filename().string();

        if (filename == ".gitignore" || filename == "README.md" ||
            filename == "CMakeLists.txt" || filename.ends_with(".exe"))
            continue;

        bool is_staged = false;
        for (const auto &f : staged)
        {
            if (f == filename)
            {
                is_staged = true;
                break;
            }
        }

        bool in_last_commit = false;
        if (!head.empty())
        {
            fs::path committed = fs::path(".my_git/commits") / head / "files" / filename;
            if (fs::exists(committed))
                in_last_commit = true;
        }

        if (is_staged)
        {
            fs::path staged_copy = fs::path(".my_git/staging") / filename;
            if (!files_equal(entry.path(), staged_copy))
            {
                std::cout << "  " << filename << "\n";
                any_modified = true;
            }
        }
        else if (in_last_commit)
        {
            fs::path committed_copy = fs::path(".my_git/commits") / head / "files" / filename;
            if (!files_equal(entry.path(), committed_copy))
            {
                std::cout << "  " << filename << "\n";
                any_modified = true;
            }
        }
    }

    if (!any_modified)
        std::cout << "  (none)\n";

    // --- Section 3: Untracked files ---
    std::cout << "\nUntracked files:\n";
    bool any_untracked = false;

    for (const auto &entry : fs::directory_iterator("."))
    {
        if (!entry.is_regular_file())
            continue;

        std::string filename = entry.path().filename().string();

        // Skip my_git's own files/folders
        if (filename == ".my_git" || entry.path().string().find(".my_git") != std::string::npos)
            continue;
        if (filename.ends_with(".exe") || filename == "CMakeLists.txt")
            continue;

        // Is it already staged?
        bool is_staged = false;
        for (const auto &f : staged)
        {
            if (f == filename)
            {
                is_staged = true;
                break;
            }
        }

        // Was it part of the last commit?
        bool in_last_commit = false;
        if (!head.empty())
        {
            fs::path committed = fs::path(".my_git/commits") / head / "files" / filename;
            if (fs::exists(committed))
                in_last_commit = true;
        }

        if (!is_staged && !in_last_commit)
        {
            std::cout << "  " << filename << "\n";
            any_untracked = true;
        }
    }

    if (!any_untracked)
        std::cout << "  (none)\n";
}

void cmd_branch(const std::string &name)
{
    fs::path ref_path = fs::path(".my_git/refs") / name;

    if (fs::exists(ref_path))
    {
        std::cout << "Error: branch '" << name << "' already exists\n";
        return;
    }

    std::string current_commit = get_head_commit();
    write_file(ref_path, current_commit);
    std::cout << "Created branch '" << name << "' at " << current_commit.substr(0, 7) << "\n";
}

void cmd_checkout(const std::string &name)
{
    fs::path ref_path = fs::path(".my_git/refs") / name;
    fs::path commit_path = fs::path(".my_git/commits") / name;

    std::string target_commit;
    bool is_branch = fs::exists(ref_path);
    bool is_commit = fs::exists(commit_path);

    if (is_branch)
    {
        target_commit = read_file(ref_path);
    }
    else if (is_commit)
    {
        target_commit = name; // raw hash, detached HEAD
    }
    else
    {
        std::cout << "Error: '" << name << "' is not a known branch or commit\n";
        return;
    }

    std::string current_commit = get_head_commit();

    // --- Remove files tracked by current commit but NOT in target commit ---
    if (!current_commit.empty())
    {
        fs::path current_files = fs::path(".my_git/commits") / current_commit / "files";
        if (fs::exists(current_files))
        {
            for (const auto &entry : fs::directory_iterator(current_files))
            {
                std::string filename = entry.path().filename().string();
                fs::path target_file = fs::path(".my_git/commits") / target_commit / "files" / filename;
                if (!fs::exists(target_file))
                {
                    fs::remove(filename);
                }
            }
        }
    }

    // --- Restore files from target commit's snapshot ---
    fs::path target_files = fs::path(".my_git/commits") / target_commit / "files";
    if (fs::exists(target_files))
    {
        for (const auto &entry : fs::directory_iterator(target_files))
        {
            fs::copy_file(entry.path(), entry.path().filename(),
                          fs::copy_options::overwrite_existing);
        }
    }

    // --- Update HEAD ---
    if (is_branch)
    {
        write_file(".my_git/HEAD", "ref: refs/" + name);
        std::cout << "Switched to branch '" << name << "'\n";
    }
    else
    {
        write_file(".my_git/HEAD", target_commit); // raw hash = detached
        std::cout << "Note: switching to '" << target_commit.substr(0, 7) << "'.\n";
        std::cout << "You are in 'detached HEAD' state. Any new commits made now\n";
        std::cout << "may not belong to any branch.\n";
    }
}

void cmd_branch_list()
{
    std::string current_ref = get_current_branch_ref(); // e.g. "refs/main", or "" if detached

    for (const auto &entry : fs::directory_iterator(".my_git/refs"))
    {
        std::string branch_name = entry.path().filename().string();
        std::string this_ref = "refs/" + branch_name;

        if (this_ref == current_ref)
        {
            std::cout << "* " << branch_name << "\n";
        }
        else
        {
            std::cout << "  " << branch_name << "\n";
        }
    }

    if (current_ref.empty())
    {
        std::string head_hash = get_head_commit();
        std::cout << "* (HEAD detached at " << head_hash.substr(0, 7) << ")\n";
    }
}

void cmd_diff(const std::string &hash1, const std::string &hash2)
{
    fs::path files1 = fs::path(".my_git/commits") / hash1 / "files";
    fs::path files2 = fs::path(".my_git/commits") / hash2 / "files";

    if (!fs::exists(files1) || !fs::exists(files2))
    {
        std::cout << "Error: one or both commit hashes not found\n";
        return;
    }

    // Collect union of filenames across both snapshots
    std::vector<std::string> all_filenames;
    for (const auto &entry : fs::directory_iterator(files1))
        all_filenames.push_back(entry.path().filename().string());
    for (const auto &entry : fs::directory_iterator(files2))
    {
        std::string fname = entry.path().filename().string();
        if (std::find(all_filenames.begin(), all_filenames.end(), fname) == all_filenames.end())
            all_filenames.push_back(fname);
    }
    std::sort(all_filenames.begin(), all_filenames.end());

    for (const auto &fname : all_filenames)
    {
        std::string old_content = read_file(files1 / fname); // "" if file didn't exist
        std::string new_content = read_file(files2 / fname);

        if (old_content == new_content)
            continue; // no change in this file

        std::cout << "diff --my_git a/" << fname << " b/" << fname << "\n";

        auto a = split_lines(old_content);
        auto b = split_lines(new_content);
        auto diff = diff_lines(a, b);

        for (const auto &dl : diff)
        {
            if (dl.type == ' ')
                std::cout << "  " << dl.text << "\n";
            else
                std::cout << dl.type << " " << dl.text << "\n";
        }
        std::cout << "\n";
    }
}

void cmd_merge(const std::string &branch_name)
{
    fs::path ref_path = fs::path(".my_git/refs") / branch_name;
    if (!fs::exists(ref_path))
    {
        std::cout << "Error: branch '" << branch_name << "' does not exist\n";
        return;
    }

    std::string ours = get_head_commit();
    std::string theirs = read_file(ref_path);
    std::string base = find_merge_base(ours, theirs);

    write_file(".my_git/MERGE_HEAD", theirs);

    std::cout << "Merging '" << branch_name << "' into current branch\n";
    std::cout << "Base: " << base.substr(0, 7) << "  Ours: " << ours.substr(0, 7)
              << "  Theirs: " << theirs.substr(0, 7) << "\n\n";

    // Collect union of filenames across all 3 snapshots
    std::set<std::string> all_files;
    for (auto *h : {&base, &ours, &theirs})
    {
        fs::path dir = fs::path(".my_git/commits") / *h / "files";
        if (fs::exists(dir))
            for (const auto &entry : fs::directory_iterator(dir))
                all_files.insert(entry.path().filename().string());
    }

    bool any_conflict = false;

    for (const auto &filename : all_files)
    {
        std::string base_content = get_file_at_commit(base, filename);
        std::string ours_content = get_file_at_commit(ours, filename);
        std::string theirs_content = get_file_at_commit(theirs, filename);

        if (ours_content == theirs_content)
        {
            // Identical on both sides (or both unchanged) -> nothing to do
            continue;
        }
        if (base_content == ours_content)
        {
            // Only 'theirs' changed -> take theirs
            write_file(filename, theirs_content);
            std::cout << "Updated (from " << branch_name << "): " << filename << "\n";
            continue;
        }
        if (base_content == theirs_content)
        {
            // Only 'ours' changed -> keep ours (already in working dir, but ensure it's written)
            write_file(filename, ours_content);
            std::cout << "Kept (ours): " << filename << "\n";
            continue;
        }

        // Both changed -> CONFLICT
        any_conflict = true;
        std::string merged = "<<<<<<< HEAD\n" + ours_content + "=======\n" + theirs_content + ">>>>>>> " + branch_name + "\n";
        write_file(filename, merged);
        std::cout << "CONFLICT: " << filename << "\n";
    }

    if (any_conflict)
    {
        std::cout << "\nAutomatic merge failed; fix conflicts, then run:\n";
        std::cout << "  my_git add <file>\n";
        std::cout << "  my_git commit \"merge message\"\n";
    }
    else
    {
        std::cout << "\nMerge successful (no conflicts). Run:\n";
        std::cout << "  my_git add <file>\n";
        std::cout << "  my_git commit \"merge message\"\n";
        std::cout << "to record the merge commit.\n";
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: my_git <command>\n";
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "init")
        cmd_init();
    else if (cmd == "help")
    {
        std::cout << "my_git - a simplified version control system\n\n";
        std::cout << "Commands:\n";
        std::cout << "  init      Create empty repository\n";
        std::cout << "  add       Stage file for commit\n";
        std::cout << "  commit    Save staged changes permanently\n";
        std::cout << "  log       Show commit history (linear)\n";
        std::cout << "  status    Show staged/modified/untracked files\n";
        std::cout << "  branch    Create or list branches\n";
        std::cout << "  checkout  Switch branch or commit\n";
        std::cout << "  diff      Compare two commit snapshots\n";
        std::cout << "  merge     Combine another branch's changes\n";
        std::cout << "  graph     Display commit DAG with branches\n";
    }
    else if (cmd == "graph")
        cmd_graph();
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
        cmd_log();
    else if (cmd == "status")
        cmd_status();
    else if (cmd == "branch")
    {
        if (argc < 3)
        {
            cmd_branch_list();
        }
        else
        {
            cmd_branch(argv[2]);
        }
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
    else if (cmd == "selftest")
    {
        std::cout << "SHA1(\"hello\") = " << sha1("hello") << "\n";

        std::vector<std::string> a = {"Hello World", "Line2", "Line3"};
        std::vector<std::string> b = {"Hello Git", "Line2", "Line3"};
        auto dp = build_lcs_table(a, b);
        std::cout << "LCS length test = " << dp[dp.size() - 1][dp[0].size() - 1] << "\n";

        if (fs::exists(".my_git/refs/main") && fs::exists(".my_git/refs/feature"))
        {
            std::string base = find_merge_base(read_file(".my_git/refs/main"), read_file(".my_git/refs/feature"));
            std::cout << "Merge base test = " << base.substr(0, 7) << "\n";
        }
    }
    else
        std::cout << "Unknown command: " << cmd << "\n";

    return 0;
}