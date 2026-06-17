#include "core/diff_engine.h"
#include <sstream>
#include <algorithm>

std::vector<std::string> split_lines(const std::string &content)
{
    std::vector<std::string> lines;
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line))
        lines.push_back(line);
    return lines;
}

std::vector<std::vector<int>> build_lcs_table(const std::vector<std::string> &a, const std::vector<std::string> &b)
{
    int n = a.size(), m = b.size();
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
    for (int i = 1; i <= n; i++)
        for (int j = 1; j <= m; j++)
            if (a[i-1] == b[j-1])
                dp[i][j] = dp[i-1][j-1] + 1;
            else
                dp[i][j] = std::max(dp[i-1][j], dp[i][j-1]);
    return dp;
}

std::vector<DiffLine> diff_lines(const std::vector<std::string> &a, const std::vector<std::string> &b)
{
    auto dp = build_lcs_table(a, b);
    std::vector<DiffLine> result;
    int i = a.size(), j = b.size();

    while (i > 0 && j > 0)
    {
        if (a[i-1] == b[j-1])
        {
            result.push_back({' ', a[i-1]});
            i--; j--;
        }
        else if (dp[i-1][j] > dp[i][j-1])
        {
            result.push_back({'-', a[i-1]});
            i--;
        }
        else
        {
            result.push_back({'+', b[j-1]});
            j--;
        }
    }
    while (i > 0) { result.push_back({'-', a[i-1]}); i--; }
    while (j > 0) { result.push_back({'+', b[j-1]}); j--; }

    std::reverse(result.begin(), result.end());
    return result;
}
