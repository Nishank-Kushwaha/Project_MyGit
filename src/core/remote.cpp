#include "core/remote.h"
#include "utils/file_io.h"
#include <filesystem>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

std::string get_remote_url(const std::string &name)
{
    std::string config = read_file(".my_git/config");
    std::istringstream iss(config);
    std::string line;
    std::string prefix = "remote." + name + ".url=";
    while (std::getline(iss, line))
    {
        if (line.rfind(prefix, 0) == 0)
        {
            std::string url = trim_nl(line.substr(prefix.size()));

            if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0)
                return url; // HTTP URLs are used as-is, no path resolution

            fs::path p(url);
            if (p.is_relative())
                url = fs::absolute(p).string();
            std::replace(url.begin(), url.end(), '\\', '/');
            return url;
        }
    }
    return "";
}
