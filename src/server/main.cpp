#include "third_party/httplib.h"
#include "utils/file_io.h"
#include "utils/base64.h"
#include "core/object_store.h"
#include "core/commits.h"
#include <iostream>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

std::string g_repo_path;

void list_refs_recursive(const fs::path &dir, const std::string &prefix, std::vector<std::string> &entries)
{
    if (!fs::exists(dir))
        return;

    for (const auto &entry : fs::directory_iterator(dir))
    {
        std::string name = entry.path().filename().string();
        std::string rel_name = prefix.empty() ? name : prefix + "/" + name;

        if (entry.is_directory())
        {
            list_refs_recursive(entry.path(), rel_name, entries);
        }
        else if (entry.is_regular_file())
        {
            std::string hash = trim_nl(read_file(entry.path()));
            if (hash.empty())
                continue;

            std::ostringstream obj;
            obj << "{\"name\":\"" << rel_name << "\",\"hash\":\"" << hash << "\"}";
            entries.push_back(obj.str());
        }
    }
}

static std::string json_extract_string(const std::string &body, const std::string &key, size_t from, size_t &end_pos)
{
    std::string needle = "\"" + key + "\":\"";
    size_t key_pos = body.find(needle, from);
    if (key_pos == std::string::npos)
    {
        end_pos = std::string::npos;
        return "";
    }
    size_t val_start = key_pos + needle.size();
    size_t val_end = body.find('"', val_start);
    end_pos = val_end + 1;
    return body.substr(val_start, val_end - val_start);
}

static std::vector<std::string> json_extract_string_array(const std::string &body, const std::string &key, size_t from, size_t &end_pos)
{
    std::vector<std::string> result;
    std::string needle = "\"" + key + "\":[";
    size_t key_pos = body.find(needle, from);
    if (key_pos == std::string::npos)
    {
        end_pos = std::string::npos;
        return result;
    }

    size_t pos = key_pos + needle.size();
    size_t array_end = body.find(']', pos);
    if (array_end == std::string::npos)
    {
        end_pos = std::string::npos;
        return result;
    }

    while (pos < array_end)
    {
        size_t quote_start = body.find('"', pos);
        if (quote_start == std::string::npos || quote_start > array_end)
            break;
        size_t quote_end = body.find('"', quote_start + 1);
        if (quote_end == std::string::npos || quote_end > array_end)
            break;

        result.push_back(body.substr(quote_start + 1, quote_end - quote_start - 1));
        pos = quote_end + 1;
    }

    end_pos = array_end + 1;
    return result;
}

void write_raw_object(const std::string &hash, const std::string &raw_bytes)
{
    if (object_exists(hash))
        return;

    fs::path obj_dir = fs::path(".my_git/objects") / hash.substr(0, 2);
    fs::path obj_path = obj_dir / hash.substr(2);

    fs::create_directories(obj_dir);
    write_file(obj_path, raw_bytes);
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cout << "Usage: my_git_server <port> <repo_path>\n";
        return 1;
    }

    int port = std::atoi(argv[1]);
    g_repo_path = argv[2];

    fs::path repo_root(g_repo_path);
    fs::path my_git_dir = repo_root / ".my_git";

    if (!fs::exists(my_git_dir))
    {
        std::cout << "Error: '" << g_repo_path << "' is not a my_git repository (.my_git not found)\n";
        return 1;
    }

    std::string repo_root_abs = fs::absolute(repo_root).string();

    fs::current_path(repo_root);

    httplib::Server svr;

    // GET /  ->  simple liveness check
    svr.Get("/", [](const httplib::Request &, httplib::Response &res)
            { res.set_content("Hello from my_git server", "text/plain"); });

    // GET /refs  ->  JSON array of every branch ref
    svr.Get("/refs", [](const httplib::Request &, httplib::Response &res)
            {
        std::vector<std::string> entries;
        list_refs_recursive(".my_git/refs", "", entries);

        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < entries.size(); ++i)
        {
            json << entries[i];
            if (i + 1 < entries.size())
                json << ",";
        }
        json << "]";

        res.set_content(json.str(), "application/json"); });

    // GET /refs/<name>  ->  raw commit hash text
    svr.Get(R"(/refs/(.+))", [](const httplib::Request &req, httplib::Response &res)
            {
        std::string ref_name = req.matches[1];
        fs::path ref_path = fs::path(".my_git/refs") / ref_name;

        std::string hash = trim_nl(read_file(ref_path));
        if (hash.empty())
        {
            res.status = 404;
            res.set_content("ref not found: " + ref_name, "text/plain");
            return;
        }

        res.set_content(hash, "text/plain"); });

    // GET /commit/<hash>  ->  raw metadata text of that commit
    svr.Get(R"(/commit/(.+))", [](const httplib::Request &req, httplib::Response &res)
            {
        std::string hash = req.matches[1];
        fs::path metadata_path = fs::path(".my_git/commits") / hash / "metadata";

        std::string metadata = read_file(metadata_path);
        if (metadata.empty())
        {
            res.status = 404;
            res.set_content("commit not found: " + hash, "text/plain");
            return;
        }

        res.set_content(metadata, "text/plain"); });

    // GET /object/<hash>  ->  raw on-disk bytes of that object
    svr.Get(R"(/object/(.+))", [](const httplib::Request &req, httplib::Response &res)
            {
        std::string hash = req.matches[1];
        if (hash.size() < 3)
        {
            res.status = 400;
            res.set_content("invalid object hash: " + hash, "text/plain");
            return;
        }

        fs::path obj_path = fs::path(".my_git/objects") / hash.substr(0, 2) / hash.substr(2);

        std::string raw = read_file(obj_path);
        if (raw.empty())
        {
            res.status = 404;
            res.set_content("object not found: " + hash, "text/plain");
            return;
        }

        res.set_content(raw, "application/octet-stream"); });

    // POST /push  ->  accept commits + objects, update a branch ref
    svr.Post("/push", [](const httplib::Request &req, httplib::Response &res)
             {
        const std::string &body = req.body;
        size_t pos = 0;

        std::string branch = json_extract_string(body, "branch", 0, pos);
        if (branch.empty())
        {
            res.status = 400;
            res.set_content("{\"error\":\"missing branch\"}", "application/json");
            return;
        }

        // --- Parse and write objects ---
        size_t objects_section = body.find("\"objects\":[");
        if (objects_section != std::string::npos)
        {
            size_t search_pos = objects_section;
            while (true)
            {
                size_t hash_end;
                std::string hash = json_extract_string(body, "hash", search_pos, hash_end);
                if (hash.empty() || hash_end == std::string::npos)
                    break;

                size_t data_end;
                std::string data_b64 = json_extract_string(body, "data_base64", hash_end, data_end);
                if (data_end == std::string::npos)
                    break;

                std::string raw_bytes = base64_decode(data_b64);
                write_raw_object(hash, raw_bytes);

                search_pos = data_end;

                // stop once we've moved past the objects array
                size_t next_obj = body.find("\"hash\"", search_pos);
                size_t commits_section = body.find("\"commits\":[");
                if (next_obj == std::string::npos || (commits_section != std::string::npos && next_obj > commits_section))
                    break;
            }
        }

        // --- Parse and write commits ---
        size_t commits_section = body.find("\"commits\":[");
        if (commits_section != std::string::npos)
        {
            size_t search_pos = commits_section;
            while (true)
            {
                size_t hash_end;
                std::string hash = json_extract_string(body, "hash", search_pos, hash_end);
                if (hash.empty() || hash_end == std::string::npos)
                    break;

                size_t meta_end;
                std::string metadata = json_extract_string(body, "metadata", hash_end, meta_end);
                if (meta_end == std::string::npos)
                    break;

                // un-escape \n that the client encoded as literal backslash-n
                std::string unescaped;
                for (size_t i = 0; i < metadata.size(); ++i)
                {
                    if (metadata[i] == '\\' && i + 1 < metadata.size() && metadata[i + 1] == 'n')
                    {
                        unescaped += '\n';
                        i++;
                    }
                    else
                    {
                        unescaped += metadata[i];
                    }
                }

                fs::path commit_dir = fs::path(".my_git/commits") / hash;
                if (!fs::exists(commit_dir / "metadata"))
                {
                    fs::create_directories(commit_dir);
                    write_file(commit_dir / "metadata", unescaped);
                }

                search_pos = meta_end;
            }
        }

        // --- Determine new tip: last commit hash sent (client sends in child->ancestor order,
        //     so the FIRST commit in the array is the new tip) ---
        size_t first_hash_pos;
        std::string new_tip = json_extract_string(body, "hash", commits_section, first_hash_pos);

        if (new_tip.empty())
        {
            res.status = 400;
            res.set_content("{\"error\":\"no commits provided\"}", "application/json");
            return;
        }

        // --- Fast-forward check ---
        fs::path ref_path = fs::path(".my_git/refs") / branch;
        std::string current_tip = fs::exists(ref_path) ? trim_nl(read_file(ref_path)) : "";

        if (!current_tip.empty() && current_tip != new_tip)
        {
            std::string merge_base = find_merge_base(current_tip, new_tip);
            if (merge_base != current_tip)
            {
                res.status = 409;
                res.set_content("{\"error\":\"non-fast-forward, rejected\"}", "application/json");
                return;
            }
        }

        // --- Update ref ---
        fs::create_directories(ref_path.parent_path());
        write_file(ref_path, new_tip);

        std::ostringstream resp;
        resp << "{\"branch\":\"" << branch << "\",\"new_tip\":\"" << new_tip << "\",\"status\":\"ok\"}";
        res.set_content(resp.str(), "application/json"); });

    // POST /have  ->  given candidate commit/object hashes, report which ones the server already has, so the client can skip sending them.
    svr.Post("/have", [](const httplib::Request &req, httplib::Response &res)
             {
        const std::string &body = req.body;

        size_t pos_after_commits;
        std::vector<std::string> commit_hashes = json_extract_string_array(body, "commit_hashes", 0, pos_after_commits);

        size_t pos_after_objects;
        std::vector<std::string> object_hashes = json_extract_string_array(body, "object_hashes", 0, pos_after_objects);

        std::vector<std::string> have_commits;
        for (const auto &hash : commit_hashes)
        {
            fs::path metadata_path = fs::path(".my_git/commits") / hash / "metadata";
            if (fs::exists(metadata_path))
                have_commits.push_back(hash);
        }

        std::vector<std::string> have_objects;
        for (const auto &hash : object_hashes)
        {
            if (object_exists(hash))
                have_objects.push_back(hash);
        }

        std::ostringstream json;
        json << "{\"have_commits\":[";
        for (size_t i = 0; i < have_commits.size(); ++i)
        {
            json << "\"" << have_commits[i] << "\"";
            if (i + 1 < have_commits.size())
                json << ",";
        }
        json << "],\"have_objects\":[";
        for (size_t i = 0; i < have_objects.size(); ++i)
        {
            json << "\"" << have_objects[i] << "\"";
            if (i + 1 < have_objects.size())
                json << ",";
        }
        json << "]}";

        res.set_content(json.str(), "application/json"); });

    std::cout << "my_git server listening on port " << port << "\n";
    std::cout << "Serving repository: " << repo_root_abs << "\n";
    svr.listen("0.0.0.0", port);

    return 0;
}