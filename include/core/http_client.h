#pragma once
#include <string>
#include <vector>
#include <utility>
#include <set>

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

// Pushes the local commit chain starting at local_tip to the remote's <branch>, over HTTP.
// Only sends commits/objects the remote doesn't already have (based on remote's current tip).
// Returns: 0 = success, 1 = non-fast-forward rejected by server, 2 = network/other failure.
int http_push_branch(const std::string &base_url, const std::string &branch, const std::string &local_tip);

// Asks the remote which of the given candidate commit/object hashes it already has.
// Fills have_commits / have_objects with the subset the server reports owning.
// Returns false on connection failure (outputs left untouched).
bool http_check_have(const std::string &base_url, const std::vector<std::string> &commit_hashes, const std::vector<std::string> &object_hashes, std::set<std::string> &have_commits, std::set<std::string> &have_objects);