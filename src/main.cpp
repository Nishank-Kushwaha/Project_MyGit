#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <vector>
#include <chrono>
#include <ctime>
#include <sstream>

namespace fs = std::filesystem;

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

// Read HEAD -> returns current commit id, or "" if no commits yet
std::string get_head()
{
    return read_file(".my_git/HEAD");
}

// Update HEAD to point to a new commit id
void set_head(const std::string &commit_id)
{
    write_file(".my_git/HEAD", commit_id);
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

// Initialize my_git
void cmd_init()
{
    fs::create_directories(".my_git/commits");
    fs::create_directories(".my_git/staging");
    std::ofstream(".my_git/HEAD").close();
    std::ofstream(".my_git/index").close();
    std::cout << "Initialized empty my_git repository\n";
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

    std::string parent = get_head();
    std::string timestamp = current_timestamp();

    // --- Build the full snapshot content first (in memory) ---
    // We need this to know what to hash AND what to write to disk

    // Step A: figure out the final set of files (parent's files + staged overlays)
    std::vector<std::string> all_files;    // filenames in final snapshot
    std::vector<std::string> all_contents; // matching contents

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

    // --- Build hash input: metadata + all file contents (sorted for determinism) ---
    std::string hash_input = "message: " + message + "\n" + "timestamp: " + timestamp + "\n" + "parent: " + parent + "\n";
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
    std::string metadata = "message: " + message + "\n" + "timestamp: " + timestamp + "\n" + "parent: " + parent + "\n";
    write_file(commit_dir / "metadata", metadata);

    // Update HEAD
    set_head(new_id);

    // Clear staging area and index
    for (const auto &filename : staged)
    {
        fs::path staged_file = fs::path(".my_git/staging") / fs::path(filename).filename();
        fs::remove(staged_file);
    }
    write_file(".my_git/index", "");

    std::cout << "[commit " << new_id.substr(0, 7) << "] " << message << "\n";
}

void cmd_log()
{
    std::string current = get_head();

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
    std::string head = get_head();

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
        std::cout << "Commands: init, add, commit, log, status\n";
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
    else
        std::cout << "Unknown command: " << cmd << "\n";

    return 0;
}