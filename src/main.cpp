#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <vector>
#include <chrono>
#include <ctime>
#include <sstream>
#include <set>
#include <iomanip>
#include <map>
#include <queue>
#include <zlib.h>

namespace fs = std::filesystem;

struct DiffLine
{
    char type; // ' ' = unchanged, '-' = removed, '+' = added
    std::string text;
};

struct CommitInfo
{
    std::string hash, parent, parent2, message, timestamp;
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

// Returns the path for a given remote name, or "" if not found
std::string get_remote_url(const std::string &name)
{
    std::string config = read_file(".my_git/config");
    std::istringstream iss(config);
    std::string line;
    std::string prefix = "remote." + name + ".url=";
    while (std::getline(iss, line))
    {
        if (line.rfind(prefix, 0) == 0)
            return line.substr(prefix.size());
    }
    return "";
}

// Copies commit folder hash + all its ancestors from src_root/.my_git to dst_root/.my_git
// Returns the number of NEW commit objects copied
int copy_commits_recursive(const fs::path &src_root, const fs::path &dst_root, const std::string &start_hash)
{
    int copied = 0;
    std::string current = start_hash;

    while (!current.empty())
    {
        fs::path src_commit = src_root / ".my_git/commits" / current;
        fs::path dst_commit = dst_root / ".my_git/commits" / current;

        if (fs::exists(dst_commit))
        {
            // Already present in destination -> its ancestors must already be there too, stop
            break;
        }

        // Copy the entire commit folder (files/ + metadata)
        fs::create_directories(dst_commit);
        fs::copy(src_commit, dst_commit, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        copied++;

        // Read parent(s) to continue walking
        std::string metadata = read_file(src_commit / "metadata");
        std::istringstream iss(metadata);
        std::string line, parent, parent2;
        while (std::getline(iss, line))
        {
            if (line.rfind("parent2:", 0) == 0)
                parent2 = line.substr(9);
            else if (line.rfind("parent:", 0) == 0)
                parent = line.substr(8);
        }

        // Recurse for second parent (merge commits) separately
        if (!parent2.empty())
        {
            copied += copy_commits_recursive(src_root, dst_root, parent2);
        }

        current = parent; // continue main chain
    }
    return copied;
}

// Compress a string using zlib's deflate
std::string zlib_compress(const std::string &input)
{
    uLongf bound = compressBound(input.size());
    std::vector<uint8_t> out(bound);

    int res = compress2(out.data(), &bound,
                        reinterpret_cast<const uint8_t *>(input.data()), input.size(),
                        Z_BEST_COMPRESSION);
    if (res != Z_OK)
        return ""; // shouldn't happen for in-memory buffers

    return std::string(reinterpret_cast<char *>(out.data()), bound);
}

// Decompress a zlib-compressed string. original_size must be known ahead of time.
std::string zlib_decompress(const std::string &compressed, size_t original_size)
{
    std::vector<uint8_t> out(original_size);
    uLongf out_len = original_size;

    int res = uncompress(out.data(), &out_len,
                         reinterpret_cast<const uint8_t *>(compressed.data()), compressed.size());
    if (res != Z_OK)
        return "";

    return std::string(reinterpret_cast<char *>(out.data()), out_len);
}

// Real Git's object storage layout: .my_git/objects/<first 2 hex chars>/<remaining 38 hex chars>
std::string write_object(const std::string &content, const std::string &type)
{
    std::string header = type + " " + std::to_string(content.size()) + '\0';
    std::string full = header + content;
    std::string hash = sha1(full);

    fs::path obj_dir = fs::path(".my_git/objects") / hash.substr(0, 2);
    fs::path obj_path = obj_dir / hash.substr(2);

    if (!fs::exists(obj_path))
    {
        fs::create_directories(obj_dir);
        std::string compressed_body = zlib_compress(content);
        // Store: header (uncompressed) + 4-byte compressed-length + compressed body
        std::string to_write = header;
        uint32_t comp_len = (uint32_t)compressed_body.size();
        to_write.append(reinterpret_cast<char *>(&comp_len), 4);
        to_write += compressed_body;
        write_file(obj_path, to_write);
    }
    return hash;
}

// Returns the FULL stored content (including "<type> <size>\0" header)
std::string read_object(const std::string &hash)
{
    fs::path obj_path = fs::path(".my_git/objects") / hash.substr(0, 2) / hash.substr(2);
    std::string raw = read_file(obj_path);
    if (raw.empty())
        return "";

    size_t null_pos = raw.find('\0');
    if (null_pos == std::string::npos)
        return "";

    std::string header = raw.substr(0, null_pos + 1); // "<type> <size>\0"

    // Parse original content size from header
    size_t space_pos = header.find(' ');
    size_t orig_size = std::stoul(header.substr(space_pos + 1, null_pos - space_pos - 1));

    // Read compressed length (4 bytes after header)
    uint32_t comp_len;
    memcpy(&comp_len, raw.data() + null_pos + 1, 4);

    std::string compressed_body = raw.substr(null_pos + 1 + 4, comp_len);
    std::string content = zlib_decompress(compressed_body, orig_size);

    return header + content; // same return format as before (header + content)
}

// Builds a tree object representing all regular files directly inside `dir_path`.
// Returns the tree object's hash.
std::string write_tree(const fs::path &dir_path)
{
    std::vector<std::pair<std::string, std::string>> entries; // (filename, blob_hash)

    for (const auto &entry : fs::directory_iterator(dir_path))
    {
        if (!entry.is_regular_file())
            continue;
        std::string filename = entry.path().filename().string();
        std::string content = read_file(entry.path());
        std::string blob_hash = write_object(content, "blob");
        entries.push_back({filename, blob_hash});
    }

    // Sort by filename for determinism (same set of files -> same tree hash)
    std::sort(entries.begin(), entries.end());

    std::string tree_content;
    for (auto &[name, hash] : entries)
    {
        tree_content += "100644 blob " + hash + " " + name + "\n";
    }

    return write_object(tree_content, "tree");
}

std::map<std::string, std::string> load_tree_recursive(const std::string &tree_hash)
{
    std::map<std::string, std::string> result; // path -> content

    std::string raw = read_object(tree_hash);
    if (raw.empty())
        return result;

    size_t null_pos = raw.find('\0');
    std::string tree_content = raw.substr(null_pos + 1);

    std::istringstream iss(tree_content);
    std::string line;
    while (std::getline(iss, line))
    {
        if (line.empty())
            continue;
        // format: "<mode> <type> <hash> <name>"
        std::istringstream ls(line);
        std::string mode, type, hash, name;
        ls >> mode >> type >> hash;
        std::getline(ls, name); // rest of line (handles spaces in filenames)
        if (!name.empty() && name[0] == ' ')
            name = name.substr(1);

        if (type == "blob")
        {
            std::string blob_raw = read_object(hash);
            size_t bn = blob_raw.find('\0');
            result[name] = blob_raw.substr(bn + 1);
        }
        else if (type == "tree")
        {
            // Recurse into subdirectory (forward-compatible; current trees are flat)
            auto sub = load_tree_recursive(hash);
            for (auto &[subpath, content] : sub)
                result[name + "/" + subpath] = content;
        }
    }
    return result;
}

std::map<std::string, std::string> reconstruct_commit(const std::string &commit_hash)
{
    std::string metadata = read_file(fs::path(".my_git/commits") / commit_hash / "metadata");
    std::istringstream iss(metadata);
    std::string line, tree_hash;
    while (std::getline(iss, line))
    {
        if (line.rfind("tree:", 0) == 0)
        {
            tree_hash = line.substr(6);
            while (!tree_hash.empty() && (tree_hash.back() == '\n' || tree_hash.back() == '\r' || tree_hash.back() == ' '))
                tree_hash.pop_back();
        }
    }
    if (tree_hash.empty())
        return {};
    return load_tree_recursive(tree_hash);
}

std::string trim_nl(std::string s)
{
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}

bool object_exists(const std::string &hash)
{
    if (hash.empty())
        return false;
    fs::path p = fs::path(".my_git/objects") / hash.substr(0, 2) / hash.substr(2);
    return fs::exists(p);
}

// Recursively verify a tree object and everything it references
void check_tree_recursive(const std::string &tree_hash, int &errors)
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

int run_fsck_checks()
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

void cmd_graph()
{
    // ─── 1. Load commits ────────────────────────────────────────────
    std::map<std::string, CommitInfo> commits;

    if (!fs::exists(".my_git/commits"))
    {
        std::cout << "(no commits)\n";
        return;
    }

    for (const auto &entry : fs::directory_iterator(".my_git/commits"))
    {
        CommitInfo ci;
        ci.hash = entry.path().filename().string();
        std::istringstream iss(read_file(entry.path() / "metadata"));
        std::string ln;
        while (std::getline(iss, ln))
        {
            if (ln.rfind("parent2:", 0) == 0)
                ci.parent2 = ln.substr(9);
            else if (ln.rfind("parent:", 0) == 0)
                ci.parent = ln.substr(8);
            else if (ln.rfind("message:", 0) == 0)
                ci.message = ln.substr(9);
            else if (ln.rfind("timestamp:", 0) == 0)
                ci.timestamp = ln.substr(11);
        }
        for (auto *f : {&ci.parent, &ci.parent2, &ci.message, &ci.timestamp})
            while (!f->empty() && (f->back() == '\n' || f->back() == '\r' || f->back() == ' '))
                f->pop_back();
        commits[ci.hash] = ci;
    }
    if (commits.empty())
    {
        std::cout << "(no commits)\n";
        return;
    }

    // ─── 2. Topological sort (Kahn, newest-tip first via timestamp PQ) ─
    std::map<std::string, int> child_cnt;
    for (auto &[h, c] : commits)
    {
        child_cnt.emplace(h, 0);
        if (!c.parent.empty())
            child_cnt[c.parent];
        if (!c.parent2.empty())
            child_cnt[c.parent2];
    }
    for (auto &[h, c] : commits)
    {
        if (!c.parent.empty())
            child_cnt[c.parent]++;
        if (!c.parent2.empty())
            child_cnt[c.parent2]++;
    }

    auto ts_cmp = [&](const std::string &a, const std::string &b)
    {
        return commits[a].timestamp < commits[b].timestamp; // max-heap
    };
    std::priority_queue<std::string, std::vector<std::string>,
                        decltype(ts_cmp)>
        pq(ts_cmp);
    for (auto &[h, cnt] : child_cnt)
        if (cnt == 0 && commits.count(h))
            pq.push(h);

    std::vector<std::string> order;
    while (!pq.empty())
    {
        auto h = pq.top();
        pq.pop();
        order.push_back(h);
        auto rel = [&](const std::string &p)
        {
            if (p.empty() || !commits.count(p))
                return;
            if (--child_cnt[p] == 0)
                pq.push(p);
        };
        rel(commits[h].parent);
        rel(commits[h].parent2);
    }

    // ─── 3. Resolve HEAD ────────────────────────────────────────────
    std::string head_hash;
    if (fs::exists(".my_git/HEAD"))
    {
        std::string h = read_file(".my_git/HEAD");
        while (!h.empty() && (h.back() == '\n' || h.back() == '\r' || h.back() == ' '))
            h.pop_back();
        if (h.rfind("ref: refs/", 0) == 0)
        {
            std::string ref = ".my_git/refs/" + h.substr(10);
            if (fs::exists(ref))
            {
                head_hash = read_file(ref);
                while (!head_hash.empty() &&
                       (head_hash.back() == '\n' || head_hash.back() == '\r'))
                    head_hash.pop_back();
            }
        }
        else
        {
            head_hash = h;
        }
    }
    std::string lane0_seed = head_hash.empty() ? (order.empty() ? "" : order[0]) : head_hash;

    // ─── 4. Lane assignment ─────────────────────────────────────────
    // sim[i] = commit hash lane i is currently tracking  ("" = dead)
    std::vector<std::string> sim;
    std::map<std::string, int> commit_lane;

    if (!lane0_seed.empty())
    {
        sim.push_back(lane0_seed);
        commit_lane[lane0_seed] = 0;
    }

    struct RowInfo
    {
        std::string hash;
        int my_lane, p1_lane, p2_lane;
    };
    std::vector<RowInfo> rows;

    for (auto &h : order)
    {
        int my_lane = -1;
        for (int i = 0; i < (int)sim.size(); i++)
            if (sim[i] == h)
            {
                my_lane = i;
                break;
            }
        if (my_lane == -1)
        {
            my_lane = (int)sim.size();
            sim.push_back(h);
        }
        commit_lane[h] = my_lane;

        auto &ci = commits[h];
        RowInfo ri{h, my_lane, -1, -1};

        if (!ci.parent.empty())
        {
            int ex = -1;
            for (int i = 0; i < (int)sim.size(); i++)
                if (i != my_lane && sim[i] == ci.parent)
                {
                    ex = i;
                    break;
                }
            if (ex == -1)
            {
                sim[my_lane] = ci.parent;
                ri.p1_lane = my_lane;
            }
            else
            {
                sim[my_lane] = "";
                ri.p1_lane = ex;
            }
        }
        else
        {
            sim[my_lane] = "";
        }

        if (!ci.parent2.empty())
        {
            int ex = -1;
            for (int i = 0; i < (int)sim.size(); i++)
                if (sim[i] == ci.parent2)
                {
                    ex = i;
                    break;
                }
            if (ex == -1)
            {
                ri.p2_lane = (int)sim.size();
                sim.push_back(ci.parent2);
            }
            else
            {
                ri.p2_lane = ex;
            }
        }
        rows.push_back(ri);
    }

    int total_lanes = (int)sim.size();

    // ─── 5. Live-range computation ──────────────────────────────────
    std::vector<int> lane_first(total_lanes, (int)rows.size());
    std::vector<int> lane_last(total_lanes, -1);

    std::map<std::string, int> hash_to_row;
    for (int r = 0; r < (int)rows.size(); r++)
        hash_to_row[rows[r].hash] = r;

    for (int r = 0; r < (int)rows.size(); r++)
    {
        auto &ri = rows[r];
        lane_first[ri.my_lane] = std::min(lane_first[ri.my_lane], r);
        lane_last[ri.my_lane] = std::max(lane_last[ri.my_lane], r);
        if (ri.p2_lane != -1 && hash_to_row.count(commits[ri.hash].parent2))
        {
            int p2r = hash_to_row[commits[ri.hash].parent2];
            lane_first[ri.p2_lane] = std::min(lane_first[ri.p2_lane], r + 1);
            lane_last[ri.p2_lane] = std::max(lane_last[ri.p2_lane], p2r);
        }
    }

    std::vector<std::vector<bool>> live(
        rows.size(), std::vector<bool>(total_lanes, false));
    for (int i = 0; i < total_lanes; i++)
        for (int r = lane_first[i]; r <= lane_last[i] && r < (int)rows.size(); r++)
            live[r][i] = true;

    auto is_live = [&](int r, int i) -> bool
    {
        return r >= 0 && r < (int)live.size() && i >= 0 && i < (int)live[r].size() && live[r][i];
    };

    // ─── 6. Branch labels ───────────────────────────────────────────
    std::map<std::string, std::vector<std::string>> branch_labels;
    if (fs::exists(".my_git/refs"))
        for (const auto &e : fs::directory_iterator(".my_git/refs"))
        {
            if (!fs::is_regular_file(e))
                continue;
            std::string tip = read_file(e.path());
            while (!tip.empty() &&
                   (tip.back() == '\n' || tip.back() == '\r' || tip.back() == ' '))
                tip.pop_back();
            branch_labels[tip].push_back(e.path().filename().string());
        }

    // ─── 7. Column renderer ─────────────────────────────────────────
    // Converts a cell array into a printable string (no trailing newline).
    // Cell chars: ' ' | '|' | '*' | '\' | '/' | '_'
    //
    // Column width = 3: [char][trail0][trail1]
    //   trail = "--" when a horizontal bridge exits the RIGHT edge of this cell.
    //   A bridge exits rightward when:
    //     - current cell is '|', '*', or '_'  (can emit right)
    //     - AND next cell is '_', '/', or '\'  (is part of a bridge)
    auto cells_to_string = [](const std::vector<char> &cells) -> std::string
    {
        std::string out;
        int W = (int)cells.size();
        for (int i = 0; i < W; i++)
        {
            char c = cells[i];
            bool trail_dash = false;
            if (i + 1 < W)
            {
                char nx = cells[i + 1];
                bool cur_emits = (c == '|' || c == '_' || c == '*');
                bool next_is_bridge = (nx == '_' || nx == '/' || nx == '\\');
                if (cur_emits && next_is_bridge)
                    trail_dash = true;
            }
            if (c == ' ')
                out += "   ";
            else if (trail_dash)
                out += std::string(1, c) + "--";
            else
                out += std::string(1, c) + "  ";
        }
        return out;
    };

    // ─── 8. Render rows ─────────────────────────────────────────────
    int W = total_lanes;

    for (int r = 0; r < (int)rows.size(); r++)
    {
        auto &ri = rows[r];

        // ── Commit line (with annotation) ───────────────────────────
        {
            std::vector<char> cells(W, ' ');
            for (int i = 0; i < W; i++)
                cells[i] = (i == ri.my_lane) ? '*'
                           : is_live(r, i)   ? '|'
                                             : ' ';

            std::string line = cells_to_string(cells);
            // Annotation: short hash, (HEAD), branch labels, message
            line += ri.hash.substr(0, 7);
            if (ri.hash == head_hash)
                line += " (HEAD)";
            if (branch_labels.count(ri.hash))
                for (auto &b : branch_labels[ri.hash])
                    line += " [" + b + "]";
            line += "  \"" + commits[ri.hash].message + "\"";
            std::cout << line << '\n';
        }

        if (r + 1 >= (int)rows.size())
            break;

        // ── Connector row ────────────────────────────────────────────
        bool is_merge = (ri.p2_lane != -1);
        bool lane_dies = (ri.p1_lane != -1 && ri.p1_lane != ri.my_lane);

        std::vector<char> conn(W, ' ');

        // Base: straight pipes for uninvolved live lanes
        for (int i = 0; i < W; i++)
        {
            if (!is_live(r, i) || !is_live(r + 1, i))
                continue;
            bool involved = (is_merge && (i == ri.my_lane || i == ri.p2_lane)) || (lane_dies && (i == ri.my_lane || i == ri.p1_lane));
            if (!involved)
                conn[i] = '|';
        }

        // Merge: p2_lane (always to the right of my_lane) bends left
        if (is_merge)
        {
            int L = std::min(ri.my_lane, ri.p2_lane);
            int R = std::max(ri.my_lane, ri.p2_lane);
            if (ri.p2_lane > ri.my_lane)
            {
                conn[R] = '\\';
                for (int i = L + 1; i < R; i++)
                    conn[i] = '_';
                conn[L] = '|'; // main lane keeps going
            }
            else
            {
                conn[L] = '/';
                for (int i = L + 1; i < R; i++)
                    conn[i] = '_';
                conn[R] = '|';
            }
        }

        // Lane dies: my_lane folds toward p1_lane
        if (lane_dies)
        {
            int L = std::min(ri.my_lane, ri.p1_lane);
            int R = std::max(ri.my_lane, ri.p1_lane);
            if (ri.p1_lane < ri.my_lane)
            { // fold left
                conn[R] = '/';
                for (int i = L + 1; i < R; i++)
                    conn[i] = '_';
                if (conn[L] == ' ')
                    conn[L] = '|';
            }
            else
            { // fold right (unusual)
                conn[L] = '\\';
                for (int i = L + 1; i < R; i++)
                    conn[i] = '_';
                if (conn[R] == ' ')
                    conn[R] = '|';
            }
        }

        std::cout << cells_to_string(conn) << '\n';
    }

    // ─── 9. Branch summary ──────────────────────────────────────────
    std::cout << "\nBranches:\n";
    if (fs::exists(".my_git/refs"))
        for (const auto &e : fs::directory_iterator(".my_git/refs"))
        {
            if (!fs::is_regular_file(e))
                continue;
            std::string tip = read_file(e.path());
            while (!tip.empty() &&
                   (tip.back() == '\n' || tip.back() == '\r' || tip.back() == ' '))
                tip.pop_back();
            std::cout << "  " << std::left << std::setw(16)
                      << e.path().filename().string()
                      << " -> " << tip.substr(0, 7) << '\n';
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

    // CHANGED: copy-forward via reconstruct_commit instead of reading files/
    if (!parent.empty())
    {
        auto parent_snapshot = reconstruct_commit(parent);
        for (auto &[filename, content] : parent_snapshot)
        {
            all_files.push_back(filename);
            all_contents.push_back(content);
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

    // NEW: build a tree object for this snapshot (Phase 7)
    std::vector<std::pair<std::string, std::string>> tree_entries;
    for (size_t i = 0; i < all_files.size(); ++i)
    {
        std::string blob_hash = write_object(all_contents[i], "blob");
        tree_entries.push_back({all_files[i], blob_hash});
    }
    std::sort(tree_entries.begin(), tree_entries.end());

    std::string tree_content;
    for (auto &[name, hash] : tree_entries)
        tree_content += "100644 blob " + hash + " " + name + "\n";

    std::string tree_hash = write_object(tree_content, "tree");

    // --- Build hash input: metadata + all file contents ---
    std::string hash_input = "message: " + message + "\n" + "timestamp: " + timestamp + "\n" + "parent: " + parent + "\n" + "parent2: " + parent2 + "\n" + "tree: " + tree_hash + "\n"; // NEW
    for (size_t i = 0; i < all_files.size(); ++i)
    {
        hash_input += all_files[i] + ":" + all_contents[i] + "\n";
    }

    std::string new_id = sha1(hash_input);

    // CHANGED: only create the commit directory for metadata — no files/ subfolder
    fs::path commit_dir = fs::path(".my_git/commits") / new_id;
    fs::create_directories(commit_dir);

    // Write metadata
    std::string metadata = "message: " + message + "\n" + "timestamp: " + timestamp + "\n" + "parent: " + parent + "\n" + "parent2: " + parent2 + "\n" + "tree: " + tree_hash + "\n";
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

    // NEW: reconstruct HEAD's snapshot from objects instead of reading files/
    std::map<std::string, std::string> head_snapshot;
    if (!head.empty())
        head_snapshot = reconstruct_commit(head);

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

        bool in_last_commit = head_snapshot.count(filename) > 0; // CHANGED

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
            std::string working_content = read_file(entry.path());
            if (working_content != head_snapshot[filename]) // CHANGED: in-memory compare
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
        bool in_last_commit = head_snapshot.count(filename) > 0; // CHANGED

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

    // CHANGED: reconstruct both snapshots from objects instead of reading files/
    std::map<std::string, std::string> current_snapshot;
    if (!current_commit.empty())
        current_snapshot = reconstruct_commit(current_commit);
    std::map<std::string, std::string> target_snapshot = reconstruct_commit(target_commit);

    // --- Remove files tracked by current commit but NOT in target commit ---
    for (auto &[filename, content] : current_snapshot)
    {
        if (target_snapshot.count(filename) == 0)
        {
            fs::remove(filename);
        }
    }

    // --- Restore files from target commit's snapshot ---
    for (auto &[filename, content] : target_snapshot)
    {
        fs::path p(filename);
        if (p.has_parent_path())
            fs::create_directories(p.parent_path()); // forward-compat for nested paths
        write_file(filename, content);
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
        if (!entry.is_regular_file())
            continue; // <-- skip "remotes/" subfolder
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
    if (!fs::exists(fs::path(".my_git/commits") / hash1 / "metadata") ||
        !fs::exists(fs::path(".my_git/commits") / hash2 / "metadata"))
    {
        std::cout << "Error: one or both commit hashes not found\n";
        return;
    }

    auto snap1 = reconstruct_commit(hash1); // old
    auto snap2 = reconstruct_commit(hash2); // new

    // Union of filenames (std::map keeps them sorted already)
    std::set<std::string> all_filenames;
    for (auto &[name, _] : snap1)
        all_filenames.insert(name);
    for (auto &[name, _] : snap2)
        all_filenames.insert(name);

    for (const auto &fname : all_filenames)
    {
        std::string old_content = snap1.count(fname) ? snap1[fname] : ""; // "" if file didn't exist
        std::string new_content = snap2.count(fname) ? snap2[fname] : ""; // "" if file didn't exist

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

    // CHANGED: reconstruct all three snapshots from objects instead of reading files/
    auto base_snapshot = base.empty() ? std::map<std::string, std::string>{} : reconstruct_commit(base);
    auto ours_snapshot = reconstruct_commit(ours);
    auto theirs_snapshot = reconstruct_commit(theirs);

    // Union of filenames across all 3 snapshots
    std::set<std::string> all_files;
    for (auto &[name, _] : base_snapshot)
        all_files.insert(name);
    for (auto &[name, _] : ours_snapshot)
        all_files.insert(name);
    for (auto &[name, _] : theirs_snapshot)
        all_files.insert(name);

    bool any_conflict = false;

    for (const auto &filename : all_files)
    {
        std::string base_content = base_snapshot.count(filename) ? base_snapshot[filename] : "";
        std::string ours_content = ours_snapshot.count(filename) ? ours_snapshot[filename] : "";
        std::string theirs_content = theirs_snapshot.count(filename) ? theirs_snapshot[filename] : "";

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

void cmd_remote_add(const std::string &name, const std::string &path)
{
    std::string config = read_file(".my_git/config");
    config += "remote." + name + ".url=" + path + "\n";
    write_file(".my_git/config", config);
    std::cout << "Added remote '" << name << "' -> " << path << "\n";
}

void cmd_push(const std::string &remote_name, const std::string &branch_name)
{
    std::string remote_path = get_remote_url(remote_name);
    if (remote_path.empty())
    {
        std::cout << "Error: remote '" << remote_name << "' not found\n";
        return;
    }

    fs::path remote_root = remote_path;
    if (!fs::exists(remote_root / ".my_git"))
    {
        std::cout << "Error: '" << remote_path << "' is not a my_git repository\n";
        return;
    }

    fs::path local_ref = fs::path(".my_git/refs") / branch_name;
    if (!fs::exists(local_ref))
    {
        std::cout << "Error: branch '" << branch_name << "' does not exist locally\n";
        return;
    }

    std::string local_hash = read_file(local_ref);
    fs::path remote_ref = remote_root / ".my_git/refs" / branch_name;

    // Fast-forward check
    if (fs::exists(remote_ref))
    {
        std::string remote_hash = read_file(remote_ref);

        if (!remote_hash.empty())
        { // <-- NEW: only check if remote actually has a commit
            if (remote_hash == local_hash)
            {
                std::cout << "Already up to date.\n";
                return;
            }
            std::set<std::string> local_ancestors = get_ancestors(local_hash);
            if (!local_ancestors.count(remote_hash))
            {
                std::cout << "Error: push rejected (non-fast-forward). Remote has commits you don't have.\n";
                return;
            }
        }
    }

    // Copy missing commit objects
    int copied = copy_commits_recursive(".", remote_root, local_hash);

    // Update remote's ref
    write_file(remote_ref, local_hash);

    std::cout << "Pushed '" << branch_name << "' to '" << remote_name << "' ("
              << copied << " new commit object(s))\n";
}

void cmd_fetch(const std::string &remote_name)
{
    std::string remote_path = get_remote_url(remote_name);
    if (remote_path.empty())
    {
        std::cout << "Error: remote '" << remote_name << "' not found\n";
        return;
    }

    fs::path remote_root = remote_path;
    if (!fs::exists(remote_root / ".my_git"))
    {
        std::cout << "Error: '" << remote_path << "' is not a my_git repository\n";
        return;
    }

    fs::path remote_refs_dir = remote_root / ".my_git/refs";
    fs::path local_tracking_dir = fs::path(".my_git/refs/remotes") / remote_name;
    fs::create_directories(local_tracking_dir);

    int total_copied = 0;
    int branches_updated = 0;

    for (const auto &entry : fs::directory_iterator(remote_refs_dir))
    {
        if (!entry.is_regular_file())
            continue; // skip "remotes" subfolder if present

        std::string branch_name = entry.path().filename().string();
        std::string remote_hash = read_file(entry.path());
        if (remote_hash.empty())
            continue; // empty branch, nothing to fetch

        // Copy missing commit objects FROM remote TO local
        total_copied += copy_commits_recursive(remote_root, ".", remote_hash);

        // Update local remote-tracking ref
        fs::path tracking_ref = local_tracking_dir / branch_name;
        std::string old_hash = fs::exists(tracking_ref) ? read_file(tracking_ref) : "";
        if (old_hash != remote_hash)
        {
            write_file(tracking_ref, remote_hash);
            branches_updated++;
            std::cout << "  " << remote_name << "/" << branch_name
                      << "  " << (old_hash.empty() ? "(new)" : old_hash.substr(0, 7))
                      << " -> " << remote_hash.substr(0, 7) << "\n";
        }
    }

    std::cout << "Fetched from '" << remote_name << "': " << branches_updated
              << " branch(es) updated, " << total_copied << " new commit object(s)\n";
}

void cmd_pull(const std::string &remote_name, const std::string &branch_name)
{
    std::cout << "Pulling from '" << remote_name << "'...\n";
    cmd_fetch(remote_name);

    std::string tracking_ref = "remotes/" + remote_name + "/" + branch_name;

    if (!fs::exists(fs::path(".my_git/refs") / tracking_ref))
    {
        std::cout << "Error: nothing to merge (no tracking ref for '" << tracking_ref << "')\n";
        return;
    }

    std::cout << "\nMerging " << tracking_ref << " into current branch...\n";
    cmd_merge(tracking_ref);
}

void cmd_hash_object(const std::string &filename)
{
    if (!fs::exists(filename))
    {
        std::cout << "Error: file '" << filename << "' does not exist\n";
        return;
    }
    std::string content = read_file(filename);
    std::string hash = write_object(content, "blob");
    std::cout << hash << "\n";
}

void cmd_cat_file(const std::string &hash)
{
    std::string raw = read_object(hash);
    if (raw.empty())
    {
        std::cout << "Error: object '" << hash << "' not found\n";
        return;
    }
    // Strip "<type> <size>\0" header
    size_t null_pos = raw.find('\0');
    if (null_pos == std::string::npos)
    {
        std::cout << "Error: malformed object\n";
        return;
    }
    std::cout << raw.substr(null_pos + 1);
}

void cmd_write_tree()
{
    std::string hash = write_tree(".");
    std::cout << hash << "\n";
}

void cmd_ls_tree(const std::string &hash)
{
    std::string raw = read_object(hash);
    if (raw.empty())
    {
        std::cout << "Error: object '" << hash << "' not found\n";
        return;
    }
    size_t null_pos = raw.find('\0');
    std::cout << raw.substr(null_pos + 1); // print entries as-is
}

void cmd_fsck()
{
    int errors = run_fsck_checks();
    if (errors == 0)
        std::cout << "fsck: repository is healthy (0 errors)\n";
    else
        std::cout << "\nfsck: " << errors << " error(s) found\n";
}

void cmd_cleanup_snapshots()
{
    std::cout << "Running integrity check before cleanup...\n";
    int errors = run_fsck_checks();
    if (errors > 0)
    {
        std::cout << "\nAborting cleanup: " << errors << " integrity error(s) found. Run 'fsck' for details.\n";
        return;
    }
    std::cout << "Repository healthy. Proceeding with cleanup.\n\n";

    int removed = 0, skipped = 0;
    for (const auto &entry : fs::directory_iterator(".my_git/commits"))
    {
        std::string hash = entry.path().filename().string();
        fs::path files_dir = entry.path() / "files";
        if (!fs::exists(files_dir))
            continue;

        auto reconstructed = reconstruct_commit(hash);
        std::map<std::string, std::string> from_files;
        for (const auto &f : fs::directory_iterator(files_dir))
            from_files[f.path().filename().string()] = read_file(f.path());

        if (reconstructed != from_files)
        {
            std::cout << "[SKIP] " << hash.substr(0, 7) << " - reconstruction mismatch, not removing\n";
            skipped++;
            continue;
        }

        fs::remove_all(files_dir);
        removed++;
    }
    std::cout << "Removed files/ from " << removed << " commit(s)";
    if (skipped > 0)
        std::cout << ", skipped " << skipped;
    std::cout << "\n";
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
        std::cout << "my_git - a simplified version control system\n";
        std::cout << "Usage: my_git <command> [<args>]\n";
        std::cout << "These are the available my_git commands:\n\n";

        std::vector<std::pair<std::string, std::string>> commands = {
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
            {"hash-object", "Compute and store a blob object"},
            {"cat-file", "Print contents of an object"},
            {"write-tree", "Build a tree object from directory"},
            {"ls-tree", "List entries of a tree object"},
        };

        for (const auto &[name, desc] : commands)
        {
            std::cout << "  " << std::left << std::setw(15) << name << desc << "\n";
        }

        std::cout << "\nRun 'my_git <command>' with no arguments to see usage details (if applicable).\n";
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
        // Usage: my_git cat-file -p <hash>
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
    else if (cmd == "verify-objects")
    {
        int total = 0, ok = 0;
        for (const auto &entry : fs::directory_iterator(".my_git/commits"))
        {
            std::string hash = entry.path().filename().string();
            auto reconstructed = reconstruct_commit(hash);

            fs::path files_dir = entry.path() / "files";
            if (!fs::exists(files_dir))
            {
                total++;
                ok++;
                std::cout << "[PASS] " << hash.substr(0, 7)
                          << "  (" << reconstructed.size() << " files via objects, no files/ -- object-only commit)\n";
                continue;
            }

            std::map<std::string, std::string> from_files;
            for (const auto &f : fs::directory_iterator(files_dir))
                from_files[f.path().filename().string()] = read_file(f.path());

            total++;
            bool match = (reconstructed == from_files);
            if (match)
                ok++;
            std::cout << (match ? "[PASS] " : "[FAIL] ") << hash.substr(0, 7)
                      << "  (" << reconstructed.size() << " files via objects, "
                      << from_files.size() << " via files/)\n";
        }
        std::cout << "\n"
                  << ok << "/" << total << " commits verified\n";
    }
    else if (cmd == "fsck")
        cmd_fsck();
    else if (cmd == "cleanup-snapshots")
        cmd_cleanup_snapshots();
    else if (cmd == "selftest")
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

        std::cout << "\n"
                  << passed << "/" << total << " checks passed\n";
    }
    else
        std::cout << "Unknown command: " << cmd << "\n";

    return 0;
}