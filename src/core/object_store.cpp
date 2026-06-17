#include "core/object_store.h"
#include "utils/sha1.h"
#include "utils/file_io.h"
#include <filesystem>
#include <vector>
#include <cstdint>
#include <cstring>
#include <zlib.h>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

std::string zlib_compress(const std::string &input)
{
    uLongf bound = compressBound(input.size());
    std::vector<uint8_t> out(bound);
    int res = compress2(out.data(), &bound,
                        reinterpret_cast<const uint8_t *>(input.data()), input.size(),
                        Z_BEST_COMPRESSION);
    if (res != Z_OK)
        return "";
    return std::string(reinterpret_cast<char *>(out.data()), bound);
}

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
        std::string to_write = header;
        uint32_t comp_len = (uint32_t)compressed_body.size();
        to_write.append(reinterpret_cast<char *>(&comp_len), 4);
        to_write += compressed_body;
        write_file(obj_path, to_write);
    }
    return hash;
}

std::string read_object(const std::string &hash)
{
    fs::path obj_path = fs::path(".my_git/objects") / hash.substr(0, 2) / hash.substr(2);
    std::string raw = read_file(obj_path);
    if (raw.empty())
        return "";

    size_t null_pos = raw.find('\0');
    if (null_pos == std::string::npos)
        return "";

    std::string header = raw.substr(0, null_pos + 1);
    size_t space_pos = header.find(' ');
    size_t orig_size = std::stoul(header.substr(space_pos + 1, null_pos - space_pos - 1));

    uint32_t comp_len;
    memcpy(&comp_len, raw.data() + null_pos + 1, 4);

    std::string compressed_body = raw.substr(null_pos + 1 + 4, comp_len);
    std::string content = zlib_decompress(compressed_body, orig_size);

    return header + content;
}

std::string write_tree(const fs::path &dir_path)
{
    std::vector<std::pair<std::string, std::string>> entries;
    for (const auto &entry : fs::directory_iterator(dir_path))
    {
        if (!entry.is_regular_file())
            continue;
        std::string filename = entry.path().filename().string();
        std::string content = read_file(entry.path());
        std::string blob_hash = write_object(content, "blob");
        entries.push_back({filename, blob_hash});
    }
    std::sort(entries.begin(), entries.end());
    std::string tree_content;
    for (auto &[name, hash] : entries)
        tree_content += "100644 blob " + hash + " " + name + "\n";
    return write_object(tree_content, "tree");
}

std::map<std::string, std::string> load_tree_recursive(const std::string &tree_hash)
{
    std::map<std::string, std::string> result;
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
        std::istringstream ls(line);
        std::string mode, type, hash, name;
        ls >> mode >> type >> hash;
        std::getline(ls, name);
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

std::string build_tree_from_map(const std::map<std::string, std::string> &files)
{
    std::vector<std::pair<std::string, std::string>> tree_entries;
    std::map<std::string, std::map<std::string, std::string>> subdirs;

    for (auto &[path, content] : files)
    {
        size_t slash = path.find('/');
        if (slash == std::string::npos)
        {
            std::string blob_hash = write_object(content, "blob");
            tree_entries.push_back({path, "100644 blob " + blob_hash});
        }
        else
        {
            std::string dirname = path.substr(0, slash);
            std::string rest = path.substr(slash + 1);
            subdirs[dirname][rest] = content;
        }
    }

    for (auto &[dirname, contents] : subdirs)
    {
        std::string subtree_hash = build_tree_from_map(contents);
        tree_entries.push_back({dirname, "040000 tree " + subtree_hash});
    }

    std::sort(tree_entries.begin(), tree_entries.end());
    std::string tree_content;
    for (auto &[name, mode_type_hash] : tree_entries)
        tree_content += mode_type_hash + " " + name + "\n";
    return write_object(tree_content, "tree");
}

bool object_exists(const std::string &hash)
{
    if (hash.empty())
        return false;
    fs::path p = fs::path(".my_git/objects") / hash.substr(0, 2) / hash.substr(2);
    return fs::exists(p);
}
