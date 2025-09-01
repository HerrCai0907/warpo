#pragma once

#include "wasm.h"

namespace warpo::passes {

std::unique_ptr<wasm::Module> loadWasm(const std::vector<char> &input);

std::unique_ptr<wasm::Module> loadWat(std::string_view wat);

} // namespace warpo::passes
