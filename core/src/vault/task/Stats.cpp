#include "vault/task/Stats.hpp"
#include "vault/model/Stat.hpp"

using namespace vh::vault;

task::Stats::Stats(const unsigned int vaultId) : vaultId(vaultId) {}

void task::Stats::operator()() {
    try { promise.set_value(std::make_shared<model::Stat>(vaultId)); }
    catch (std::exception& e) { promise.set_exception(std::make_exception_ptr(e)); }
}
