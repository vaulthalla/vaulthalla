#pragma once

#include <string>

namespace vh::email {

struct RenderedEmail {
    std::string subject;
    std::string html;
    std::string text;
};

}
