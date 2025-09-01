#pragma once

#include <binaryen/src/binaryen-c.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "warpo/common/Features.hpp"

namespace warpo::frontend {

void init();

struct Config {
  std::map<std::string, std::string> uses;
  std::optional<std::string> ascWasmPath;
  common::Features features;
  std::optional<std::string> exportStart;
  bool exportRuntime;
  bool exportTable;
  std::optional<uint32_t> initialMemory;
  uint32_t optimizationLevel;
  uint32_t shrinkLevel;
};

BinaryenModuleRef compile();
BinaryenModuleRef compile(std::vector<std::string> const &entryFilePaths, Config const &config);

} // namespace warpo::frontend
