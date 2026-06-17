#pragma once
#include <string>

void cmd_hash_object(const std::string &filename);
void cmd_cat_file(const std::string &hash);
void cmd_write_tree();
void cmd_ls_tree(const std::string &hash);
