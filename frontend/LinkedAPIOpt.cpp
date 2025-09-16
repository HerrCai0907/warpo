#include <cassert>
#include <iostream>

#include "CompilerImpl.hpp"
#include "LinkedAPI.hpp"
#include "warpo/common/AsModule.hpp"

#include "src/core/common/function_traits.hpp"

namespace warpo::frontend {

namespace {

FrontendCompiler *getFrontendCompiler(vb::WasmModule *ctx) {
  return static_cast<FrontendCompiler *>(ctx->getContext());
}

void markDataElementImmutableForLink(uint32_t offset, uint32_t size, [[maybe_unused]] vb::WasmModule *ctx) {
  AsModule *const m = &getFrontendCompiler(ctx)->asModule_;
  if (m->immutableRanges_ == nullptr)
    m->immutableRanges_ = std::make_shared<ImmutableDataElementRanges>();
  m->immutableRanges_->insert({offset, offset + size});
}

} // namespace

std::vector<vb::NativeSymbol> createOptAPI() {
  return std::vector<vb::NativeSymbol>{
      STATIC_LINK("warpo", "_WarpoMarkDataElementImmutable", markDataElementImmutableForLink),
  };
}

} // namespace warpo::frontend
