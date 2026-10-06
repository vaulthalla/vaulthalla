#pragma once

#include "common/protocol.hpp"

#include <nlohmann/json.hpp>

namespace vh::helpers::cad {

// convert-step: STEP/STP (ISO 10303-21) -> binary glTF on the artifact stream.
// Options: --max-triangles (2'000'000), --max-input-bytes (512 MiB), --max-entities (10'000'000).
// Result: {"ok":true,"format":"glb","triangles":N,"nodes":N,"bbox":[minx,miny,minz,maxx,maxy,maxz],...}; the bbox is
// in model units (millimetres, OCCT's session unit), the GLB in metres, Y-up.
nlohmann::json convertStep(const Args& args, RangeClient& input, OutputSink& output);

// selftest-sandbox: tries what the sandbox must forbid and reports each outcome (tests and operators).
// Options: --write-path (a file to create), --read-path (a file outside the allowlist to read).
nlohmann::json selftestSandbox(const Args& args);

}
