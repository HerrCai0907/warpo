#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "BaseLower.hpp"
#include "CollectLeafFunction.hpp"
#include "GCInfo.hpp"
#include "LeafFunctionFilter.hpp"
#include "MergeSSA.hpp"
#include "ObjLivenessAnalyzer.hpp"
#include "OptLower.hpp"
#include "PrologEpilogInserter.hpp"
#include "SSAObj.hpp"
#include "ShrinkWrap.hpp"
#include "StackAssigner.hpp"
#include "ToStackReplacer.hpp"
#include "argparse/argparse.hpp"
#include "pass.h"
#include "passes/passes.h"
#include "support/name.h"
#include "warpo/support/Opt.hpp"
#include "wasm-builder.h"
#include "wasm-type.h"
#include "wasm.h"

#define PASS_NAME "GCOptLower"

namespace warpo::passes::gc {

static cli::Opt<bool> NoLeafFunctionFilter{
    cli::Category::OnlyForTest,
    "--no-gc-leaf-function-filter",
    [](argparse::Argument &arg) { arg.help("Disable leaf function filter during GC lowering").flag().hidden(); },
};
static cli::Opt<bool> NoMergeSSA{
    cli::Category::OnlyForTest,
    "--no-gc-merge-ssa",
    [](argparse::Argument &arg) { arg.help("Disable SSA merging during GC lowering").flag().hidden(); },
};
static cli::Opt<bool> NoOptimizedStackPositionAssigner{
    cli::Category::OnlyForTest,
    "--no-gc-optimized-stack-position-assigner",
    [](argparse::Argument &arg) {
      arg.help("Disable optimized stack position assigner during GC lowering").flag().hidden();
    },
};
static cli::Opt<bool> NoShrinkWrap{
    cli::Category::OnlyForTest,
    "--no-gc-shrink-wrap",
    [](argparse::Argument &arg) { arg.help("Disable shrink wrap during GC lowering").flag().hidden(); },
};

static cli::Opt<bool> TestOnlyControlGroup{
    cli::Category::OnlyForTest,
    "--gc-test-only-control-group",
    [](argparse::Argument &arg) { arg.flag().hidden(); },
};

struct PostLower : public wasm::Pass {
  std::shared_ptr<StackPositions const> stackPosition_;
  explicit PostLower(std::shared_ptr<StackPositions const> stackPosition) : stackPosition_(stackPosition) {
    name = "PostLower";
  }
  void run(wasm::Module *m) override {
    wasm::Builder b{*m};
    wasm::Name const memoryName = m->memories.front()->name;
    wasm::Type const i32 = wasm::Type::i32;

    addStackStackOperationFunction(m);
    uint32_t const maxShadowStackOffset = getMaxShadowStackOffset();
    for (size_t offset = 0U; offset <= maxShadowStackOffset; offset += 4U) {
      m->addFunction(b.makeFunction(
          ToStackReplacer::getToStackFunctionName(offset), wasm::Signature(i32, i32), {},
          b.makeBlock({
              b.makeStore(4, offset, 1, b.makeGlobalGet(VarStackPointer, i32), b.makeLocalGet(0, i32), i32, memoryName),
              b.makeLocalGet(0, i32),
          })));
    }

    m->removeFunction(FnLocalToStack);
    m->removeFunction(FnTmpToStack);
  }

private:
  uint32_t getMaxShadowStackOffset() const {
    uint32_t maxOffset = 0;
    for (auto const &[_, offsets] : *stackPosition_) {
      for (auto const &offset : offsets) {
        maxOffset = std::max(maxOffset, offset.second);
      }
    }
    return maxOffset;
  }
};

void OptLower::preprocess(wasm::PassRunner &runner) {
  // cleanup without changing the overall code structure
  runner.add("vacuum");
  // reduce basic blocks count to avoid to many fixed pointer calculations
  runner.add("merge-blocks");
}

void OptLower::run(wasm::Module *m) {
  wasm::PassRunner runner{getPassRunner()};

  preprocess(runner);

  if (TestOnlyControlGroup.get()) {
    // only for test purpose
    return;
  }

  ModuleLevelSSAMap const moduleLevelSSAMap = ModuleLevelSSAMap::create(m);

  std::shared_ptr<CallGraph const> const cg = CallGraphBuilder::addToPass(runner);

  std::shared_ptr<LeafFunc const> const leafFunc =
      NoLeafFunctionFilter.get() ? nullptr : LeafFunctionCollector::addToPass(runner, cg);

  std::shared_ptr<ObjLivenessInfo> const livenessInfo = ObjLivenessAnalyzer::addToPass(runner, moduleLevelSSAMap);

  if (!NoMergeSSA.get()) {
    // now merge ssa should be done firstly, it is depends on liveness info as local's possible values.
    // After LeafFunctionFilter, liveness info is not correct anymore.
    // TODO: use def-uses chain instead of liveness info
    MergeSSA::addToPass(runner, moduleLevelSSAMap, livenessInfo);
  }

  LeafFunctionFilter::addToPass(runner, leafFunc, livenessInfo);

  StackAssigner::Mode const stackAssignerMode =
      NoOptimizedStackPositionAssigner.get() ? StackAssigner::Mode::Vanilla : StackAssigner::Mode::GreedyConflictGraph;
  std::shared_ptr<StackPositions const> const stackPositions =
      StackAssigner::addToPass(runner, stackAssignerMode, livenessInfo);
  std::shared_ptr<InsertPositionHints const> const stackInsertPositions =
      NoShrinkWrap.get() ? ShrinkWrapAnalysis::dummy(runner) : ShrinkWrapAnalysis::addToPass(runner, livenessInfo);
  runner.add(std::unique_ptr<wasm::Pass>(
      new PrologEpilogInserter(stackInsertPositions, MaxShadowStackOffsetsFromStackPositions::create(stackPositions))));
  runner.add(std::unique_ptr<wasm::Pass>(new ToStackReplacer(stackPositions)));
  runner.add(std::unique_ptr<wasm::Pass>(new PostLower(stackPositions)));

  runner.run();
}

} // namespace warpo::passes::gc
