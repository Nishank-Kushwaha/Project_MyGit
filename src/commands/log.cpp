#include "commands/log.h"
#include "core/refs.h"
#include "utils/file_io.h"
#include "utils/time.h"
#include <filesystem>
#include <iostream>
#include <queue>
#include <set>
#include <vector>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

void cmd_log()
{
    std::string start = get_head_commit();

    if (start.empty())
    {
        std::cout << "No commits yet\n";
        return;
    }

    struct LogEntry
    {
        std::string hash, message, timestamp;
        time_t time;
    };

    std::queue<std::string> q;
    std::set<std::string> visited;
    std::vector<LogEntry> entries;

    q.push(start);
    visited.insert(start);

    while (!q.empty())
    {
        std::string current = q.front();
        q.pop();

        fs::path metadata_path = fs::path(".my_git/commits") / current / "metadata";
        std::string content = read_file(metadata_path);

        std::istringstream iss(content);
        std::string line, parent, parent2, timestamp, message;
        while (std::getline(iss, line))
        {
            if (line.rfind("message:", 0) == 0)
                message = line.substr(9);
            else if (line.rfind("timestamp:", 0) == 0)
                timestamp = line.substr(11);
            else if (line.rfind("parent2:", 0) == 0)
                parent2 = line.substr(9);
            else if (line.rfind("parent:", 0) == 0)
                parent = line.substr(8);
        }
        for (auto *s : {&parent, &parent2, &timestamp, &message})
            while (!s->empty() && (s->back() == '\n' || s->back() == '\r' || s->back() == ' '))
                s->pop_back();

        entries.push_back({current, message, timestamp, parse_timestamp(timestamp)});

        if (!parent.empty() && !visited.count(parent))
        {
            visited.insert(parent);
            q.push(parent);
        }
        if (!parent2.empty() && !visited.count(parent2))
        {
            visited.insert(parent2);
            q.push(parent2);
        }
    }

    // Newest -> oldest. Ties keep whatever relative order std::sort gives them.
    std::sort(entries.begin(), entries.end(),
              [](const LogEntry &a, const LogEntry &b)
              { return a.time > b.time; });

    for (const auto &e : entries)
    {
        std::cout << "commit " << e.hash << "\n";
        std::cout << "    " << e.message << "\n";
        std::cout << "Date:   " << e.timestamp << "\n\n";
    }
}
