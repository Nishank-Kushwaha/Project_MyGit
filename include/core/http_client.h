#pragma once
#include <string>
#include <vector>
#include <utility>

bool is_http_url(const std::string &url);

// Returns list of {name, hash} pairs from GET /refs. Empty vector on failure.
std::vector<std::pair<std::string, std::string>> http_get_refs(const std::string &base_url);

// Returns raw hash text from GET /refs/<name>. Empty string if 404 or connection failure.
std::string http_get_ref(const std::string &base_url, const std::string &ref_name);

// Returns raw metadata text from GET /commit/<hash>. Empty string if 404 or connection failure.
std::string http_get_commit_metadata(const std::string &base_url, const std::string &hash);

// Returns raw on-disk object bytes from GET /object/<hash>. Empty string if 404 or connection failure.
std::string http_get_object(const std::string &base_url, const std::string &hash);

// Fetches a full commit chain (commits + trees + blobs) over HTTP into local .my_git/.
// Walks parent/parent2 links starting at start_hash, skipping anything already present locally.
// Returns the number of new commit objects written.
int http_fetch_commits(const std::string &base_url, const std::string &start_hash);