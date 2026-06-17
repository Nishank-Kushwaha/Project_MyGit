#include "utils/time.h"
#include <chrono>
#include <map>
#include <cstdio>

std::string current_timestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::string s = std::ctime(&t);
    if (!s.empty() && s.back() == '\n')
        s.pop_back();
    return s;
}

time_t parse_timestamp(const std::string &ts)
{
    static const std::map<std::string, int> months = {
        {"Jan", 0}, {"Feb", 1}, {"Mar", 2}, {"Apr", 3}, {"May", 4}, {"Jun", 5},
        {"Jul", 6}, {"Aug", 7}, {"Sep", 8}, {"Oct", 9}, {"Nov", 10}, {"Dec", 11}};

    char wd[4] = {0}, mon[4] = {0};
    int day = 0, hour = 0, min = 0, sec = 0, year = 0;

    int matched = sscanf(ts.c_str(), "%3s %3s %d %d:%d:%d %d",
                         wd, mon, &day, &hour, &min, &sec, &year);
    if (matched != 7) return 0;

    auto it = months.find(mon);
    if (it == months.end()) return 0;

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon  = it->second;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min  = min;
    tm.tm_sec  = sec;
    tm.tm_isdst = -1;

    return mktime(&tm);
}
