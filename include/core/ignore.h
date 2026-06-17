#pragma once
#include <string>
#include <vector>

std::vector<std::string> load_ignore_rules();

bool matches_ignore_rule(const std::string &relpath, const std::vector<std::string> &rules);
