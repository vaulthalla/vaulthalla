#pragma once

#include <memory>
#include <variant>

namespace vh::vault::model { struct Stat; }

typedef std::variant<bool, std::shared_ptr<vh::vault::model::Stat>> ExpectedFuture;
