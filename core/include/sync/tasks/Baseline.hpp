#pragma once

#include "storage/Fwd.hpp"
#include "sync/model/Baseline.hpp"

#include <memory>

namespace vh::sync::tasks {

// After a successful transfer: what both sides now agree on (#187). Best effort: a failed write is logged and the
// transfer still counts; the next pass that sees the two sides agree writes it again.
void recordBaseline(const std::shared_ptr<storage::CloudEngine>& engine, const model::Baseline& baseline) noexcept;

}
