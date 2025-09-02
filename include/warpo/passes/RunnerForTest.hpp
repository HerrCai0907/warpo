#pragma once

#include <binaryen/src/binaryen-c.h>
#include <regex>
#include <string>

namespace warpo::passes {

std::string runOnWatForTest(std::string const &input, std::regex const &targetFunctionRegex);

} // namespace warpo::passes
