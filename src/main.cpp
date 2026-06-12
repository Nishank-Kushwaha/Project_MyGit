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

    // Determine new commit ID
    std::string parent = get_head();
    int new_id_num = parent.empty() ? 1 : std::stoi(parent) + 1;
    std::string new_id = std::to_string(new_id_num);

    // Create commit folder structure
    fs::path commit_dir = fs::path(".my_git/commits") / new_id;
    fs::create_directories(commit_dir / "files");

    // ⬇️ NEW: Copy forward the FULL snapshot from the parent commit (if any)
    if (!parent.empty())
    {
        fs::path parent_files = fs::path(".my_git/commits") / parent / "files";
        for (const auto &entry : fs::directory_iterator(parent_files))
        {
            fs::copy_file(entry.path(),
                          commit_dir / "files" / entry.path().filename(),
                          fs::copy_options::overwrite_existing);
        }
    }
    // ⬆️ END NEW BLOCK

    // Copy each staged file into the commit's "files" folder (overlay new/changed files)
    for (const auto &filename : staged)
    {
        fs::path src = fs::path(".my_git/staging") / fs::path(filename).filename();
        fs::path dst = commit_dir / "files" / fs::path(filename).filename();
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
    }

    // Write metadata
    std::string metadata = "message: " + message + "\n" + "timestamp: " + current_timestamp() + "\n" + "parent: " + parent + "\n";
    write_file(commit_dir / "metadata", metadata);

    // Update HEAD to point to the new commit
    set_head(new_id);

    // Clear the staging area and index
    for (const auto &filename : staged)
    {
        fs::path staged_file = fs::path(".my_git/staging") / fs::path(filename).filename();
        fs::remove(staged_file);
    }
    write_file(".my_git/index", ""); // empty the index

    std::cout << "[commit " << new_id << "] " << message << "\n";
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