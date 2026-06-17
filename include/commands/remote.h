#pragma once
#include <string>

void cmd_remote_add(const std::string &name, const std::string &url);
void cmd_push(const std::string &remote, const std::string &branch);
void cmd_fetch(const std::string &remote);
void cmd_pull(const std::string &remote, const std::string &branch);
void cmd_clone(const std::string &src, const std::string &dst);
