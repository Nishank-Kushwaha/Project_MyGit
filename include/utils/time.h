#pragma once
#include <string>
#include <ctime>

std::string current_timestamp();

time_t parse_timestamp(const std::string &ts);
