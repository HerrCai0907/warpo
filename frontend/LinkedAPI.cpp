#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fmt/base.h>
#include <vector>

#include "LinkedAPI.hpp"
#include "warpo/support/Container.hpp"
#include "wasm-compiler/src/core/common/NativeSymbol.hpp"

namespace warpo::frontend {

std::vector<vb::NativeSymbol> createLinkedAPI() {
  std::vector<vb::NativeSymbol> linkedAPI;
  append(linkedAPI, createAssemblyscriptAPI());
  append(linkedAPI, createCppWrapperAPI());
  append(linkedAPI, createBinaryenLinkedAPI());
  append(linkedAPI, createOptAPI());
  return linkedAPI;
}

} // namespace warpo::frontend

std::vector<vb::NativeSymbol> const &warpo::frontend::getLinkedAPI() {
  static std::vector<vb::NativeSymbol> const linkedAPI = createLinkedAPI();
  return linkedAPI;
}
