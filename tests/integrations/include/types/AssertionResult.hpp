#pragma once

#include <string>

namespace vh::test::integrations {

struct AssertionResult {
    bool ok{true};
    std::string message;
    static AssertionResult Pass() { return {true, {}}; }
    static AssertionResult Fail(std::string msg) { return {false, std::move(msg)}; }
};

}
