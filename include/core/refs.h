#pragma once
#include <string>

std::string get_current_branch_ref();

std::string get_head_commit();

void set_head_commit(const std::string &hash);
