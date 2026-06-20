#include "third_party/httplib.h"
#include <iostream>
#include <cstdlib>

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: my_git_server <port>\n";
        return 1;
    }

    int port = std::atoi(argv[1]);

    httplib::Server svr;

    svr.Get("/", [](const httplib::Request &, httplib::Response &res)
            { res.set_content("Hello from my_git server", "text/plain"); });

    std::cout << "my_git server listening on port " << port << "\n";
    svr.listen("0.0.0.0", port);

    return 0;
}
