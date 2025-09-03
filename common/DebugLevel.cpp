#include "warpo/common/DebugLevel.hpp"
#include "warpo/support/Opt.hpp"

namespace warpo::common {

static cli::Opt<bool> optimizeLevelOption{
    cli::Category::Frontend | cli::Category::Optimization,
    "--debug",
    [](argparse::Argument &arg) -> void { arg.help("Enables debug information in emitted binaries.").flag(); },
};

} // namespace warpo::common

bool warpo::common::isEmitDebugLineInfo() { return optimizeLevelOption.get(); }
