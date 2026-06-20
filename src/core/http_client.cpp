#include "core/http_client.h"
#include "core/object_store.h"
#include "core/commits.h"
#include "utils/file_io.h"
#include "utils/base64.h"
#include "third_party/httplib.h"
#include <filesystem>
#include <sstream>
#include <iostream>
#include <set>
#include <algorithm>

namespace fs = std::filesystem;

bool is_http_url(const std::string &url)
{
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

// Splits "http://host:port" into scheme, host, port. Defaults to port 80 if missing.
static bool parse_base_url(const std::string &base_url, std::string &host, int &port)
{
    std::string rest = base_url;
    if (rest.rfind("http://", 0) == 0)
        rest = rest.substr(7);
    else if (rest.rfind("https://", 0) == 0)
        rest = rest.substr(8);

    // strip any trailing slash or path
    size_t slash = rest.find('/');
    if (slash != std::string::npos)
        rest = rest.substr(0, slash);

    size_t colon = rest.find(':');
    if (colon == std::string::npos)
    {
        host = rest;
        port = 80;
    }
    else
    {
        host = rest.substr(0, colon);
        port = std::atoi(rest.substr(colon + 1).c_str());
    }
    return !host.empty();
}

std::vector<std::pair<std::string, std::string>> http_get_refs(const std::string &base_url)
{
    std::vector<std::pair<std::string, std::string>> result;

    std::string host;
    int port;
    if (!parse_base_url(base_url, host, port))
        return result;

    httplib::Client cli(host, port);
    auto res = cli.Get("/refs");
    if (!res || res->status != 200)
        return result;

    // minimal JSON parse: [{"name":"main","hash":"abc123"}, ...]
    const std::string &body = res->body;
    size_t pos = 0;
    while (true)
    {
        size_t name_key = body.find("\"name\":\"", pos);
        if (name_key == std::string::npos)
            break;
        size_t name_start = name_key + 8;
        size_t name_end = body.find('"', name_start);
        std::string name = body.substr(name_start, name_end - name_start);

        size_t hash_key = body.find("\"hash\":\"", name_end);
        size_t hash_start = hash_key + 8;
        size_t hash_end = body.find('"', hash_start);
        std::string hash = body.substr(hash_start, hash_end - hash_start);

        result.push_back({name, hash});
        pos = hash_end + 1;
    }

    return result;
}

std::string http_get_ref(const std::string &base_url, const std::string &ref_name)
{
    std::string host;
    int port;
    if (!parse_base_url(base_url, host, port))
        return "";

    httplib::Client cli(host, port);
    auto res = cli.Get("/refs/" + ref_name);
    if (!res || res->status != 200)
        return "";

    return res->body;
}

std::string http_get_commit_metadata(const std::string &base_url, const std::string &hash)
{
    std::string host;
    int port;
    if (!parse_base_url(base_url, host, port))
        return "";

    httplib::Client cli(host, port);
    auto res = cli.Get("/commit/" + hash);
    if (!res || res->status != 200)
        return "";

    return res->body;
}

std::string http_get_object(const std::string &base_url, const std::string &hash)
{
    std::string host;
    int port;
    if (!parse_base_url(base_url, host, port))
        return "";

    httplib::Client cli(host, port);
    auto res = cli.Get("/object/" + hash);
    if (!res || res->status != 200)
        return "";

    return res->body;
}

// Writes raw on-disk object bytes directly into .my_git/objects/<aa>/<rest> without
// re-compressing (the server already sent the exact compressed on-disk format).
static void write_raw_object(const std::string &hash, const std::string &raw_bytes)
{
    if (object_exists(hash))
        return;

    fs::path obj_dir = fs::path(".my_git/objects") / hash.substr(0, 2);
    fs::path obj_path = obj_dir / hash.substr(2);

    fs::create_directories(obj_dir);
    write_file(obj_path, raw_bytes);
}

// Recursively fetches every blob/tree object referenced by a tree, starting at tree_hash.
static void fetch_tree_recursive(const std::string &base_url, const std::string &tree_hash, std::set<std::string> &visited)
{
    if (tree_hash.empty() || visited.count(tree_hash))
        return;
    visited.insert(tree_hash);

    if (!object_exists(tree_hash))
    {
        std::string raw = http_get_object(base_url, tree_hash);
        if (raw.empty())
            return;
        write_raw_object(tree_hash, raw);
    }

    // Now read it back locally (we just wrote it, or it already existed) to parse entries.
    std::string full = read_object(tree_hash);
    size_t null_pos = full.find('\0');
    if (null_pos == std::string::npos)
        return;
    std::string tree_content = full.substr(null_pos + 1);

    std::istringstream iss(tree_content);
    std::string line;
    while (std::getline(iss, line))
    {
        if (line.empty())
            continue;
        std::istringstream ls(line);
        std::string mode, type, hash;
        ls >> mode >> type >> hash;

        if (type == "blob")
        {
            if (!object_exists(hash) && !visited.count(hash))
            {
                visited.insert(hash);
                std::string raw = http_get_object(base_url, hash);
                if (!raw.empty())
                    write_raw_object(hash, raw);
            }
        }
        else if (type == "tree")
        {
            fetch_tree_recursive(base_url, hash, visited);
        }
    }
}

int http_fetch_commits(const std::string &base_url, const std::string &start_hash)
{
    int copied = 0;
    std::set<std::string> visited_objects;
    std::string current = start_hash;

    while (!current.empty())
    {
        fs::path commit_dir = fs::path(".my_git/commits") / current;

        bool already_have = fs::exists(commit_dir / "metadata");

        std::string metadata;
        if (already_have)
        {
            metadata = read_file(commit_dir / "metadata");
        }
        else
        {
            metadata = http_get_commit_metadata(base_url, current);
            if (metadata.empty())
                break;

            fs::create_directories(commit_dir);
            write_file(commit_dir / "metadata", metadata);
            copied++;
        }

        // Parse parent / parent2 / tree from metadata
        std::istringstream iss(metadata);
        std::string line, parent, parent2, tree_hash;
        while (std::getline(iss, line))
        {
            if (line.rfind("parent2:", 0) == 0)
                parent2 = trim_nl(line.substr(9));
            else if (line.rfind("parent:", 0) == 0)
                parent = trim_nl(line.substr(8));
            else if (line.rfind("tree:", 0) == 0)
                tree_hash = trim_nl(line.substr(6));
        }

        if (!tree_hash.empty())
            fetch_tree_recursive(base_url, tree_hash, visited_objects);

        if (!parent2.empty())
            copied += http_fetch_commits(base_url, parent2);

        if (already_have)
            break; // we've reached a commit we already had — ancestors are guaranteed present too

        current = parent;
    }

    return copied;
}

// ---------------------------------------------------------------------
// HTTP push
// ---------------------------------------------------------------------

// Escapes a string for safe embedding inside a JSON "..." value:
// backslash, double-quote, and newline are the only characters our
// metadata/content can realistically contain that need escaping here.
static std::string json_escape(const std::string &input)
{
    std::string out;
    out.reserve(input.size());
    for (char c : input)
    {
        switch (c)
        {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            break; // drop CR, keep LF only
        default:
            out += c;
        }
    }
    return out;
}

// Recursively collects every blob/tree object hash referenced by a tree
// that is NOT already known to be present on the remote (tracked via `known`).
static void collect_tree_objects(const std::string &tree_hash, std::set<std::string> &known, std::vector<std::string> &to_send)
{
    if (tree_hash.empty() || known.count(tree_hash))
        return;
    known.insert(tree_hash);
    to_send.push_back(tree_hash);

    std::string full = read_object(tree_hash);
    size_t null_pos = full.find('\0');
    if (null_pos == std::string::npos)
        return;
    std::string tree_content = full.substr(null_pos + 1);

    std::istringstream iss(tree_content);
    std::string line;
    while (std::getline(iss, line))
    {
        if (line.empty())
            continue;
        std::istringstream ls(line);
        std::string mode, type, hash;
        ls >> mode >> type >> hash;

        if (type == "blob")
        {
            if (!known.count(hash))
            {
                known.insert(hash);
                to_send.push_back(hash);
            }
        }
        else if (type == "tree")
        {
            collect_tree_objects(hash, known, to_send);
        }
    }
}

int http_push_branch(const std::string &base_url, const std::string &branch, const std::string &local_tip)
{
    std::string host;
    int port;
    if (!parse_base_url(base_url, host, port))
        return 2;

    // Find out what the remote already has, so we don't resend it.
    std::string remote_tip = http_get_ref(base_url, branch);

    std::set<std::string> known_commits;
    if (!remote_tip.empty())
        known_commits = get_ancestors(remote_tip);

    // Walk local commits backward from local_tip, stopping at anything the remote already has.
    std::vector<std::string> commits_to_send;
    std::set<std::string> known_objects; // objects already collected for sending (dedup within this push)
    std::vector<std::string> objects_to_send;

    std::vector<std::string> frontier = {local_tip};
    std::set<std::string> visited_commits;

    while (!frontier.empty())
    {
        std::string hash = frontier.back();
        frontier.pop_back();

        if (hash.empty() || visited_commits.count(hash) || known_commits.count(hash))
            continue;
        visited_commits.insert(hash);

        std::string metadata = read_file(fs::path(".my_git/commits") / hash / "metadata");
        if (metadata.empty())
            continue;

        commits_to_send.push_back(hash);

        std::istringstream iss(metadata);
        std::string line, parent, parent2, tree_hash;
        while (std::getline(iss, line))
        {
            if (line.rfind("parent2:", 0) == 0)
                parent2 = trim_nl(line.substr(9));
            else if (line.rfind("parent:", 0) == 0)
                parent = trim_nl(line.substr(8));
            else if (line.rfind("tree:", 0) == 0)
                tree_hash = trim_nl(line.substr(6));
        }

        if (!tree_hash.empty())
            collect_tree_objects(tree_hash, known_objects, objects_to_send);

        if (!parent.empty())
            frontier.push_back(parent);
        if (!parent2.empty())
            frontier.push_back(parent2);
    }

    if (commits_to_send.empty())
    {
        // Nothing new — still attempt the ref update in case remote_tip == local_tip already
        // (caller decides what message to print; we just report success here)
        return 0;
    }

    // ---------------------------------------------------------------
    // Smart transfer: ask the remote what it already has, and trim
    // commits_to_send / objects_to_send down to the genuine delta.
    // ---------------------------------------------------------------
    std::set<std::string> have_commits, have_objects;
    bool have_check_ok = http_check_have(base_url, commits_to_send, objects_to_send, have_commits, have_objects);

    if (have_check_ok)
    {
        std::vector<std::string> filtered_commits;
        for (const auto &h : commits_to_send)
            if (!have_commits.count(h))
                filtered_commits.push_back(h);

        std::vector<std::string> filtered_objects;
        for (const auto &h : objects_to_send)
            if (!have_objects.count(h))
                filtered_objects.push_back(h);

        // local_tip's own commit entry must always be sent even if somehow
        // already reported as "have" (e.g. re-push of same tip with new branch name)
        // since the server needs at least one commit to determine the new ref target.
        if (filtered_commits.empty() &&
            std::find(commits_to_send.begin(), commits_to_send.end(), local_tip) != commits_to_send.end())
        {
            filtered_commits.push_back(local_tip);
        }

        commits_to_send = filtered_commits;
        objects_to_send = filtered_objects;
    }
    // if the /have check fails (server unreachable for that call, or older server
    // without /have support), fall through and send the full candidate set as before —
    // correctness is preserved, we just lose the bandwidth optimization for this push.

    // Build JSON body. Client sends commits in "newest first" order (frontier search above
    // doesn't guarantee that ordering perfectly, but the server only reads the FIRST commit's
    // hash as the new tip — so we put local_tip first explicitly).
    std::ostringstream json;
    json << "{\"branch\":\"" << json_escape(branch) << "\",";

    json << "\"objects\":[";
    for (size_t i = 0; i < objects_to_send.size(); ++i)
    {
        const std::string &hash = objects_to_send[i];
        fs::path obj_path = fs::path(".my_git/objects") / hash.substr(0, 2) / hash.substr(2);
        std::string raw = read_file(obj_path); // exact on-disk bytes, same as GET /object/<hash>

        json << "{\"hash\":\"" << hash << "\",\"data_base64\":\""
             << base64_encode(raw) << "\"}";
        if (i + 1 < objects_to_send.size())
            json << ",";
    }
    json << "],";

    json << "\"commits\":[";
    // local_tip first, guaranteed
    auto it = std::find(commits_to_send.begin(), commits_to_send.end(), local_tip);
    if (it != commits_to_send.end())
        std::iter_swap(commits_to_send.begin(), it);

    for (size_t i = 0; i < commits_to_send.size(); ++i)
    {
        std::string metadata = read_file(fs::path(".my_git/commits") / commits_to_send[i] / "metadata");
        json << "{\"hash\":\"" << commits_to_send[i] << "\",\"metadata\":\""
             << json_escape(metadata) << "\"}";
        if (i + 1 < commits_to_send.size())
            json << ",";
    }
    json << "]}";

    httplib::Client cli(host, port);
    auto res = cli.Post("/push", json.str(), "application/json");

    if (!res)
        return 2;
    if (res->status == 409)
        return 1;
    if (res->status != 200)
        return 2;

    return 0;
}

// Parses a JSON array of strings from a response body: "key":["a","b"]
static std::set<std::string> parse_json_string_array(const std::string &body, const std::string &key)
{
    std::set<std::string> result;
    std::string needle = "\"" + key + "\":[";
    size_t key_pos = body.find(needle);
    if (key_pos == std::string::npos)
        return result;

    size_t pos = key_pos + needle.size();
    size_t array_end = body.find(']', pos);
    if (array_end == std::string::npos)
        return result;

    while (pos < array_end)
    {
        size_t quote_start = body.find('"', pos);
        if (quote_start == std::string::npos || quote_start > array_end)
            break;
        size_t quote_end = body.find('"', quote_start + 1);
        if (quote_end == std::string::npos || quote_end > array_end)
            break;

        result.insert(body.substr(quote_start + 1, quote_end - quote_start - 1));
        pos = quote_end + 1;
    }

    return result;
}

bool http_check_have(const std::string &base_url, const std::vector<std::string> &commit_hashes, const std::vector<std::string> &object_hashes, std::set<std::string> &have_commits, std::set<std::string> &have_objects)
{
    std::string host;
    int port;
    if (!parse_base_url(base_url, host, port))
        return false;

    std::ostringstream json;
    json << "{\"commit_hashes\":[";
    for (size_t i = 0; i < commit_hashes.size(); ++i)
    {
        json << "\"" << commit_hashes[i] << "\"";
        if (i + 1 < commit_hashes.size())
            json << ",";
    }
    json << "],\"object_hashes\":[";
    for (size_t i = 0; i < object_hashes.size(); ++i)
    {
        json << "\"" << object_hashes[i] << "\"";
        if (i + 1 < object_hashes.size())
            json << ",";
    }
    json << "]}";

    httplib::Client cli(host, port);
    auto res = cli.Post("/have", json.str(), "application/json");

    if (!res || res->status != 200)
        return false;

    have_commits = parse_json_string_array(res->body, "have_commits");
    have_objects = parse_json_string_array(res->body, "have_objects");

    return true;
}