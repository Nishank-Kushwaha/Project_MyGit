#include "third_party/httplib.h"
#include <iostream>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

std::string g_repo_path;

// Read raw file contents (binary-safe). Returns empty string if file doesn't exist.
std::string read_file_raw(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return "";
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string trim_nl(std::string s)
{
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}

// Walk .my_git/refs/ recursively, emitting one JSON object per branch ref file found.
// "prefix" accumulates subdirectory names so refs/remotes/origin/main becomes name "remotes/origin/main".
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
            std::string hash = trim_nl(read_file_raw(entry.path()));
            if (hash.empty())
                continue;

            std::ostringstream obj;
            obj << "{\"name\":\"" << rel_name << "\",\"hash\":\"" << hash << "\"}";
            entries.push_back(obj.str());
        }
    }
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

    httplib::Server svr;

    // ---------------------------------------------------------------
    // GET /  ->  simple liveness check (from Phase 13)
    // ---------------------------------------------------------------
    svr.Get("/", [](const httplib::Request &, httplib::Response &res)
            { res.set_content("Hello from my_git server", "text/plain"); });

    // ---------------------------------------------------------------
    // GET /refs  ->  JSON array of every branch ref: [{"name":"main","hash":"..."}]
    // Includes refs/remotes/<name>/<branch> as "remotes/<name>/<branch>"
    // ---------------------------------------------------------------
    svr.Get("/refs", [&my_git_dir](const httplib::Request &, httplib::Response &res)
            {
        std::vector<std::string> entries;
        list_refs_recursive(my_git_dir / "refs", "", entries);

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

    // ---------------------------------------------------------------
    // GET /refs/<name>  ->  raw commit hash text (name may contain slashes, e.g. remotes/origin/main)
    // ---------------------------------------------------------------
    svr.Get(R"(/refs/(.+))", [&my_git_dir](const httplib::Request &req, httplib::Response &res)
            {
        std::string ref_name = req.matches[1];
        fs::path ref_path = my_git_dir / "refs" / ref_name;

        std::string hash = trim_nl(read_file_raw(ref_path));
        if (hash.empty())
        {
            res.status = 404;
            res.set_content("ref not found: " + ref_name, "text/plain");
            return;
        }

        res.set_content(hash, "text/plain"); });

    // ---------------------------------------------------------------
    // GET /commit/<hash>  ->  raw metadata text of that commit
    // ---------------------------------------------------------------
    svr.Get(R"(/commit/(.+))", [&my_git_dir](const httplib::Request &req, httplib::Response &res)
            {
        std::string hash = req.matches[1];
        fs::path metadata_path = my_git_dir / "commits" / hash / "metadata";

        std::string metadata = read_file_raw(metadata_path);
        if (metadata.empty())
        {
            res.status = 404;
            res.set_content("commit not found: " + hash, "text/plain");
            return;
        }

        res.set_content(metadata, "text/plain"); });

    // ---------------------------------------------------------------
    // GET /object/<hash>  ->  raw on-disk bytes of that object (header + compressed body)
    // ---------------------------------------------------------------
    svr.Get(R"(/object/(.+))", [&my_git_dir](const httplib::Request &req, httplib::Response &res)
            {
        std::string hash = req.matches[1];
        if (hash.size() < 3)
        {
            res.status = 400;
            res.set_content("invalid object hash: " + hash, "text/plain");
            return;
        }

        fs::path obj_path = my_git_dir / "objects" / hash.substr(0, 2) / hash.substr(2);

        std::string raw = read_file_raw(obj_path);
        if (raw.empty())
        {
            res.status = 404;
            res.set_content("object not found: " + hash, "text/plain");
            return;
        }

        res.set_content(raw, "application/octet-stream"); });

    std::cout << "my_git server listening on port " << port << "\n";
    std::cout << "Serving repository: " << fs::absolute(repo_root).string() << "\n";
    svr.listen("0.0.0.0", port);

    return 0;
}