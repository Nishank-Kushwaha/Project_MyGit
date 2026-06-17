#include "commands/graph.h"
#include "core/refs.h"
#include "utils/file_io.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <set>
#include <map>
#include <vector>
#include <queue>

namespace fs = std::filesystem;

struct CommitInfo
{
    std::string hash, parent, parent2, message, timestamp;
};

void cmd_graph()
{
    // ─── 1. Load commits ────────────────────────────────────────────
    std::map<std::string, CommitInfo> commits;

    if (!fs::exists(".my_git/commits"))
    {
        std::cout << "(no commits)\n";
        return;
    }

    for (const auto &entry : fs::directory_iterator(".my_git/commits"))
    {
        CommitInfo ci;
        ci.hash = entry.path().filename().string();
        std::istringstream iss(read_file(entry.path() / "metadata"));
        std::string ln;
        while (std::getline(iss, ln))
        {
            if (ln.rfind("parent2:", 0) == 0)
                ci.parent2 = ln.substr(9);
            else if (ln.rfind("parent:", 0) == 0)
                ci.parent = ln.substr(8);
            else if (ln.rfind("message:", 0) == 0)
                ci.message = ln.substr(9);
            else if (ln.rfind("timestamp:", 0) == 0)
                ci.timestamp = ln.substr(11);
        }
        for (auto *f : {&ci.parent, &ci.parent2, &ci.message, &ci.timestamp})
            while (!f->empty() && (f->back() == '\n' || f->back() == '\r' || f->back() == ' '))
                f->pop_back();
        commits[ci.hash] = ci;
    }
    if (commits.empty())
    {
        std::cout << "(no commits)\n";
        return;
    }

    // ─── 2. Topological sort (Kahn, newest-tip first via timestamp PQ) ─
    std::map<std::string, int> child_cnt;
    for (auto &[h, c] : commits)
    {
        child_cnt.emplace(h, 0);
        if (!c.parent.empty())
            child_cnt[c.parent];
        if (!c.parent2.empty())
            child_cnt[c.parent2];
    }
    for (auto &[h, c] : commits)
    {
        if (!c.parent.empty())
            child_cnt[c.parent]++;
        if (!c.parent2.empty())
            child_cnt[c.parent2]++;
    }

    auto ts_cmp = [&](const std::string &a, const std::string &b)
    {
        return commits[a].timestamp < commits[b].timestamp; // max-heap
    };
    std::priority_queue<std::string, std::vector<std::string>,
                        decltype(ts_cmp)>
        pq(ts_cmp);
    for (auto &[h, cnt] : child_cnt)
        if (cnt == 0 && commits.count(h))
            pq.push(h);

    std::vector<std::string> order;
    while (!pq.empty())
    {
        auto h = pq.top();
        pq.pop();
        order.push_back(h);
        auto rel = [&](const std::string &p)
        {
            if (p.empty() || !commits.count(p))
                return;
            if (--child_cnt[p] == 0)
                pq.push(p);
        };
        rel(commits[h].parent);
        rel(commits[h].parent2);
    }

    // ─── 3. Resolve HEAD ────────────────────────────────────────────
    std::string head_hash;
    if (fs::exists(".my_git/HEAD"))
    {
        std::string h = read_file(".my_git/HEAD");
        while (!h.empty() && (h.back() == '\n' || h.back() == '\r' || h.back() == ' '))
            h.pop_back();
        if (h.rfind("ref: refs/", 0) == 0)
        {
            std::string ref = ".my_git/refs/" + h.substr(10);
            if (fs::exists(ref))
            {
                head_hash = read_file(ref);
                while (!head_hash.empty() &&
                       (head_hash.back() == '\n' || head_hash.back() == '\r'))
                    head_hash.pop_back();
            }
        }
        else
        {
            head_hash = h;
        }
    }
    std::string lane0_seed = head_hash.empty() ? (order.empty() ? "" : order[0]) : head_hash;

    // ─── 4. Lane assignment ─────────────────────────────────────────
    // sim[i] = commit hash lane i is currently tracking  ("" = dead)
    std::vector<std::string> sim;
    std::map<std::string, int> commit_lane;

    if (!lane0_seed.empty())
    {
        sim.push_back(lane0_seed);
        commit_lane[lane0_seed] = 0;
    }

    struct RowInfo
    {
        std::string hash;
        int my_lane, p1_lane, p2_lane;
    };
    std::vector<RowInfo> rows;

    for (auto &h : order)
    {
        int my_lane = -1;
        for (int i = 0; i < (int)sim.size(); i++)
            if (sim[i] == h)
            {
                my_lane = i;
                break;
            }
        if (my_lane == -1)
        {
            my_lane = (int)sim.size();
            sim.push_back(h);
        }
        commit_lane[h] = my_lane;

        auto &ci = commits[h];
        RowInfo ri{h, my_lane, -1, -1};

        if (!ci.parent.empty())
        {
            int ex = -1;
            for (int i = 0; i < (int)sim.size(); i++)
                if (i != my_lane && sim[i] == ci.parent)
                {
                    ex = i;
                    break;
                }
            if (ex == -1)
            {
                sim[my_lane] = ci.parent;
                ri.p1_lane = my_lane;
            }
            else
            {
                sim[my_lane] = "";
                ri.p1_lane = ex;
            }
        }
        else
        {
            sim[my_lane] = "";
        }

        if (!ci.parent2.empty())
        {
            int ex = -1;
            for (int i = 0; i < (int)sim.size(); i++)
                if (sim[i] == ci.parent2)
                {
                    ex = i;
                    break;
                }
            if (ex == -1)
            {
                ri.p2_lane = (int)sim.size();
                sim.push_back(ci.parent2);
            }
            else
            {
                ri.p2_lane = ex;
            }
        }
        rows.push_back(ri);
    }

    int total_lanes = (int)sim.size();

    // ─── 5. Live-range computation ──────────────────────────────────
    std::vector<int> lane_first(total_lanes, (int)rows.size());
    std::vector<int> lane_last(total_lanes, -1);

    std::map<std::string, int> hash_to_row;
    for (int r = 0; r < (int)rows.size(); r++)
        hash_to_row[rows[r].hash] = r;

    for (int r = 0; r < (int)rows.size(); r++)
    {
        auto &ri = rows[r];
        lane_first[ri.my_lane] = std::min(lane_first[ri.my_lane], r);
        lane_last[ri.my_lane] = std::max(lane_last[ri.my_lane], r);
        if (ri.p2_lane != -1 && hash_to_row.count(commits[ri.hash].parent2))
        {
            int p2r = hash_to_row[commits[ri.hash].parent2];
            lane_first[ri.p2_lane] = std::min(lane_first[ri.p2_lane], r + 1);
            lane_last[ri.p2_lane] = std::max(lane_last[ri.p2_lane], p2r);
        }
    }

    std::vector<std::vector<bool>> live(
        rows.size(), std::vector<bool>(total_lanes, false));
    for (int i = 0; i < total_lanes; i++)
        for (int r = lane_first[i]; r <= lane_last[i] && r < (int)rows.size(); r++)
            live[r][i] = true;

    auto is_live = [&](int r, int i) -> bool
    {
        return r >= 0 && r < (int)live.size() && i >= 0 && i < (int)live[r].size() && live[r][i];
    };

    // ─── 6. Branch labels ───────────────────────────────────────────
    std::map<std::string, std::vector<std::string>> branch_labels;
    if (fs::exists(".my_git/refs"))
        for (const auto &e : fs::directory_iterator(".my_git/refs"))
        {
            if (!fs::is_regular_file(e))
                continue;
            std::string tip = read_file(e.path());
            while (!tip.empty() &&
                   (tip.back() == '\n' || tip.back() == '\r' || tip.back() == ' '))
                tip.pop_back();
            branch_labels[tip].push_back(e.path().filename().string());
        }

    // ─── 7. Column renderer ─────────────────────────────────────────
    // Converts a cell array into a printable string (no trailing newline).
    // Cell chars: ' ' | '|' | '*' | '\' | '/' | '_'
    //
    // Column width = 3: [char][trail0][trail1]
    //   trail = "--" when a horizontal bridge exits the RIGHT edge of this cell.
    //   A bridge exits rightward when:
    //     - current cell is '|', '*', or '_'  (can emit right)
    //     - AND next cell is '_', '/', or '\'  (is part of a bridge)
    auto cells_to_string = [](const std::vector<char> &cells) -> std::string
    {
        std::string out;
        int W = (int)cells.size();
        for (int i = 0; i < W; i++)
        {
            char c = cells[i];
            bool trail_dash = false;
            if (i + 1 < W)
            {
                char nx = cells[i + 1];
                bool cur_emits = (c == '|' || c == '_' || c == '*');
                bool next_is_bridge = (nx == '_' || nx == '/' || nx == '\\');
                if (cur_emits && next_is_bridge)
                    trail_dash = true;
            }
            if (c == ' ')
                out += "   ";
            else if (trail_dash)
                out += std::string(1, c) + "--";
            else
                out += std::string(1, c) + "  ";
        }
        return out;
    };

    // ─── 8. Render rows ─────────────────────────────────────────────
    int W = total_lanes;

    for (int r = 0; r < (int)rows.size(); r++)
    {
        auto &ri = rows[r];

        // ── Commit line (with annotation) ───────────────────────────
        {
            std::vector<char> cells(W, ' ');
            for (int i = 0; i < W; i++)
                cells[i] = (i == ri.my_lane) ? '*'
                           : is_live(r, i)   ? '|'
                                             : ' ';

            std::string line = cells_to_string(cells);
            // Annotation: short hash, (HEAD), branch labels, message
            line += ri.hash.substr(0, 7);
            if (ri.hash == head_hash)
                line += " (HEAD)";
            if (branch_labels.count(ri.hash))
                for (auto &b : branch_labels[ri.hash])
                    line += " [" + b + "]";
            line += "  \"" + commits[ri.hash].message + "\"";
            std::cout << line << '\n';
        }

        if (r + 1 >= (int)rows.size())
            break;

        // ── Connector row ────────────────────────────────────────────
        bool is_merge = (ri.p2_lane != -1);
        bool lane_dies = (ri.p1_lane != -1 && ri.p1_lane != ri.my_lane);

        std::vector<char> conn(W, ' ');

        // Base: straight pipes for uninvolved live lanes
        for (int i = 0; i < W; i++)
        {
            if (!is_live(r, i) || !is_live(r + 1, i))
                continue;
            bool involved = (is_merge && (i == ri.my_lane || i == ri.p2_lane)) || (lane_dies && (i == ri.my_lane || i == ri.p1_lane));
            if (!involved)
                conn[i] = '|';
        }

        // Merge: p2_lane (always to the right of my_lane) bends left
        if (is_merge)
        {
            int L = std::min(ri.my_lane, ri.p2_lane);
            int R = std::max(ri.my_lane, ri.p2_lane);
            if (ri.p2_lane > ri.my_lane)
            {
                conn[R] = '\\';
                for (int i = L + 1; i < R; i++)
                    conn[i] = '_';
                conn[L] = '|'; // main lane keeps going
            }
            else
            {
                conn[L] = '/';
                for (int i = L + 1; i < R; i++)
                    conn[i] = '_';
                conn[R] = '|';
            }
        }

        // Lane dies: my_lane folds toward p1_lane
        if (lane_dies)
        {
            int L = std::min(ri.my_lane, ri.p1_lane);
            int R = std::max(ri.my_lane, ri.p1_lane);
            if (ri.p1_lane < ri.my_lane)
            { // fold left
                conn[R] = '/';
                for (int i = L + 1; i < R; i++)
                    conn[i] = '_';
                if (conn[L] == ' ')
                    conn[L] = '|';
            }
            else
            { // fold right (unusual)
                conn[L] = '\\';
                for (int i = L + 1; i < R; i++)
                    conn[i] = '_';
                if (conn[R] == ' ')
                    conn[R] = '|';
            }
        }

        std::cout << cells_to_string(conn) << '\n';
    }

    // ─── 9. Branch summary ──────────────────────────────────────────
    std::cout << "\nBranches:\n";
    if (fs::exists(".my_git/refs"))
        for (const auto &e : fs::directory_iterator(".my_git/refs"))
        {
            if (!fs::is_regular_file(e))
                continue;
            std::string tip = read_file(e.path());
            while (!tip.empty() &&
                   (tip.back() == '\n' || tip.back() == '\r' || tip.back() == ' '))
                tip.pop_back();
            std::cout << "  " << std::left << std::setw(16)
                      << e.path().filename().string()
                      << " -> " << tip.substr(0, 7) << '\n';
        }
}
