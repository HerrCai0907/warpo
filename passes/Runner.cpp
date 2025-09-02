#include <cassert>
#include <fmt/base.h>
#include <fmt/format.h>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "AdvancedInlining.hpp"
#include "ConditionalReturn.hpp"
#include "ExtractMostFrequentlyUsedGlobals.hpp"
#include "GC/FastLower.hpp"
#include "GC/OptLower.hpp"
#include "Runner.hpp"
#include "binaryen-c.h"
#include "parser/wat-parser.h"
#include "pass.h"
#include "warpo/common/Features.hpp"
#include "warpo/common/OptLevel.hpp"
#include "warpo/passes/Runner.hpp"
#include "wasm-binary.h"
#include "wasm-features.h"
#include "wasm-stack.h"
#include "wasm.h"

namespace warpo {

static void ensureValidate(wasm::Module &m) {
  if (!wasm::WasmValidator{}.validate(m))
    throw std::logic_error("validate error");
}

std::unique_ptr<wasm::Module> passes::loadWasm(const std::vector<char> &input) {
  std::unique_ptr<wasm::Module> m{new wasm::Module()};
  wasm::WasmBinaryReader parser(*m, common::Features::fromCLI().toBinaryenFeatureSet(), input);
  parser.read();
  ensureValidate(*m);
  return m;
}

std::unique_ptr<wasm::Module> passes::loadWat(std::string_view wat) {
  std::unique_ptr<wasm::Module> m{new wasm::Module()};
  m->features = common::Features::fromCLI().toBinaryenFeatureSet();
  auto parsed = wasm::WATParser::parseModule(*m, wat);
  if (auto *err = parsed.getErr())
    throw std::logic_error(err->msg);
  ensureValidate(*m);
  return m;
}

void passes::init() { Colors::setEnabled(false); }

static std::vector<uint8_t> outputWasm(wasm::Module *m) {
  wasm::BufferWithRandomAccess buffer;
  wasm::PassOptions options = wasm::PassOptions::getWithoutOptimization();
  wasm::WasmBinaryWriter writer(m, buffer, options);
  writer.setNamesSection(false);
  writer.setEmitModuleName(false);
  writer.write();
  return static_cast<std::vector<uint8_t>>(buffer);
}

static std::string outputWat(wasm::Module *m) {
  std::stringstream ss{};
  wasm::printStackIR(ss, m, wasm::PassOptions::getWithoutOptimization());
  return std::move(ss).str();
}

static std::unique_ptr<wasm::PassRunner> createPassRunner(wasm::Module *const m) {
  auto passRunner = std::make_unique<wasm::PassRunner>(m);
  passRunner->options.shrinkLevel = common::getShrinkLevel();
  passRunner->options.optimizeLevel = common::getOptimizationLevel();
  return passRunner;
}

static void lowering(wasm::Module *const m) {
  {
    std::unique_ptr<wasm::PassRunner> const passRunner = createPassRunner(m);
    if (passRunner->options.shrinkLevel > 0 || passRunner->options.optimizeLevel > 0)
      passRunner->add(std::unique_ptr<wasm::Pass>{new passes::gc::OptLower()});
    else
      passRunner->add(std::unique_ptr<wasm::Pass>{new passes::gc::FastLower()});
    passRunner->run();
  }
#ifndef WARPO_RELEASE_BUILD
  ensureValidate(*m);
#endif
}

static void optimize(wasm::Module *const m) {
  {
    std::unique_ptr<wasm::PassRunner> const passRunner = createPassRunner(m);
    passRunner->addDefaultOptimizationPasses();
    passRunner->add(std::unique_ptr<wasm::Pass>{passes::createAdvancedInliningPass()});
    passRunner->run();
  }
#ifndef WARPO_RELEASE_BUILD
  ensureValidate(*m);
#endif
  {
    std::unique_ptr<wasm::PassRunner> const passRunner = createPassRunner(m);
    passRunner->add(std::unique_ptr<wasm::Pass>{passes::createExtractMostFrequentlyUsedGlobalsPass()});
    passRunner->add(std::unique_ptr<wasm::Pass>{passes::createConditionalReturnPass()});
    passRunner->run();
  }
#ifndef WARPO_RELEASE_BUILD
  ensureValidate(*m);
#endif
  {
    std::unique_ptr<wasm::PassRunner> const passRunner = createPassRunner(m);
    passRunner->setDebug(false);
    passRunner->addDefaultOptimizationPasses();
    passRunner->run();
  }
  ensureValidate(*m);
}

passes::Output passes::runOnModule(BinaryenModuleRef const m) {
#ifndef WARPO_RELEASE_BUILD
  ensureValidate(*m);
#endif
  lowering(m);
  if (common::getOptimizationLevel() > 0U || common::getShrinkLevel() > 0U)
    optimize(m);
  return {.wat = outputWat(m), .wasm = outputWasm(m)};
}

passes::Output passes::runOnWat(std::string const &input) {
  std::unique_ptr<wasm::Module> m = passes::loadWat(input);
  return runOnModule(m.get());
}

} // namespace warpo
