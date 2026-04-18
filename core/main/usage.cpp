#include "usage/include/UsageManager.hpp"
#include "CommandUsage.hpp"

#include <iostream>
#include <fstream>

using namespace vh::protocols::shell;

int main(const int argc, char** argv) {
    std::string out;
    if (argc == 3 && std::string(argv[1]) == "--out") out = argv[2];

    UsageManager mgr;

    const auto md = mgr.root()->markdown();
    if (out.empty()) std::cout << md;
    else {
        std::ofstream f(out);
        f << md;
    }
    return 0;
}
