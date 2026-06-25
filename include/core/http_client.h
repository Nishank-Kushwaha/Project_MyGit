#pragma once
#include <string>
#include <vector>
#include <utility>
#include <set>

bool is_http_url(const std::string &url);
std::vector<std::pair<std::string, std::string>> http_get_refs(const std::string &base_url);
std::string http_get_ref(const std::string &base_url, const std::string &ref_name);
std::string http_get_commit_metadata(const std::string &base_url, const std::string &hash);
std::string http_get_object(const std::string &base_url, const std::string &hash);
int http_fetch_commits(const std::string &base_url, const std::string &start_hash);
int http_push_branch(const std::string &base_url, const std::string &branch, const std::string &local_tip);
bool http_check_have(const std::string &base_url, const std::vector<std::string> &commit_hashes, const std::vector<std::string> &object_hashes, std::set<std::string> &have_commits, std::set<std::string> &have_objects);