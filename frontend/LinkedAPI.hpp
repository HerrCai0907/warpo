#pragma once

#include <vector>

#include "wasm-compiler/src/core/common/NativeSymbol.hpp"

namespace warpo::frontend {

std::vector<vb::NativeSymbol> createAssemblyscriptAPI();
std::vector<vb::NativeSymbol> createBinaryenLinkedAPI();
std::vector<vb::NativeSymbol> createCppWrapperAPI();
std::vector<vb::NativeSymbol> createOptAPI();

std::vector<vb::NativeSymbol> const &getLinkedAPI();

} // namespace warpo::frontend
