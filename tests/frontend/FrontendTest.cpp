#include <algorithm>
#include <argparse/argparse.hpp>
#include <array>
#include <atomic>
#include <binaryen/src/binaryen-c.h>
#include <cassert>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fmt/base.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <support/colors.h>
#include <thread>
#include <utility>
#include <vector>

#include "warpo/frontend/Compiler.hpp"
#include "warpo/support/FileSystem.hpp"
#include "warpo/support/Opt.hpp"
#include "wasm.h"

namespace warpo {
namespace {

cli::Opt<bool> updateFlag{
    cli::Category::All,
    "-u",
    "--update",
    [](argparse::Argument &arg) { arg.help("update snapshot").flag(); },
};

enum class TestResult : uint8_t { Success, Failure, Skip };

frontend::Result compile(nlohmann::json const &configJson, std::filesystem::path const &tsPath) {
  std::vector<std::string> const entries{tsPath.string()};
  frontend::Config config = frontend::getDefaultConfig();
  config.useColorfulDiagMessage = false;
  if (configJson.contains("asc_flags")) {
    nlohmann::json::array_t const &ascFlags = configJson["asc_flags"].get<nlohmann::json::array_t>();
    for (auto const &flag : ascFlags) {
      if (flag == "--exportStart _start")
        config.exportStart = "_start";
      else if (flag == "--exportRuntime")
        config.exportRuntime = true;
      else if (flag == "--initialMemory 2")
        config.initialMemory = 2;
      else if (flag == "--use Date=")
        config.uses["Date"] = "";
      else if (flag == "--runtime incremental")
        static_cast<void>(0); // do nothing, incremental is default
      else if (flag == "--bindings raw")
        static_cast<void>(0); // do nothing, raw binding is default
    }
  }
  frontend::Result const ret = frontend::compile(entries, config);
  return ret;
}

[[nodiscard]] TestResult runUpdate(nlohmann::json const &configJson, std::filesystem::path const &tsPath,
                                   std::filesystem::path const &expectedOutPath) {
  frontend::Result const ret = compile(configJson, tsPath);
  if (ret.m == nullptr) {
    if (configJson.contains("stderr")) {
      return TestResult::Success;
    } else {
      fmt::println("FAILED '{}': expected to compile successfully\nerror message: {}", tsPath.c_str(),
                   *ret.errorMessage);
      return TestResult::Failure;
    }
  }
  std::stringstream ss;
  ss << *ret.m;
  std::string const actual = std::move(ss).str();
  writeBinaryFile(expectedOutPath, std::move(actual));
  BinaryenModuleDispose(ret.m);
  return TestResult::Success;
}

[[nodiscard]] TestResult runCompilationErrorCase(nlohmann::json const &configJson,
                                                 std::filesystem::path const &tsPath) {
  frontend::Result const ret = compile(configJson, tsPath);
  if (ret.m != nullptr) {
    fmt::println("'{}' success to compile but expect failed", tsPath.c_str());
    return TestResult::Failure;
  }
  assert(configJson.contains("stderr"));
  std::vector<std::string> expectedStderr;
  if (configJson["stderr"].is_string()) {
    expectedStderr.push_back(configJson["stderr"].get<std::string>());
  } else if (configJson["stderr"].is_array()) {
    for (auto const &item : configJson["stderr"]) {
      expectedStderr.push_back(item.get<std::string>());
    }
  }

  std::string const &actualStderr = *ret.errorMessage;
  size_t lastIndex = 0;
  bool failed = false;
  for (auto const &expectedErrorMessageLineJson : configJson["stderr"].get<nlohmann::json::array_t>()) {
    std::string const expectedErrorMessageLine = expectedErrorMessageLineJson.get<std::string>();
    size_t index = actualStderr.find(expectedErrorMessageLine, lastIndex);
    if (index == std::string::npos) {
      fmt::println("\tmissing pattern '{}' in stderr.", expectedErrorMessageLine);
      failed = true;
    } else {
      lastIndex = index + expectedErrorMessageLine.length();
    }
  }
  if (failed) {
    fmt::println("\t========== actual error message==========\n"
                 "{}"
                 "\t========== actual error message==========",
                 actualStderr);
    fmt::println("FAILED '{}': stderr mismatch", tsPath.c_str());
    return TestResult::Failure;
  }
  return TestResult::Success;
}

[[nodiscard]] TestResult runSnapshotCase(nlohmann::json const &configJson, std::filesystem::path const &tsPath,
                                         std::filesystem::path const &expectedOutPath) {
  frontend::Result const ret = compile(configJson, tsPath);
  if (ret.m == nullptr) {
    fmt::println("FAILED '{}': expected to compile successfully\nerror message: {}", tsPath.c_str(), *ret.errorMessage);
    return TestResult::Failure;
  }
  std::stringstream ss;
  ss << *ret.m;
  std::string const actual = std::move(ss).str();
  std::string const expected = readTextFile(expectedOutPath);
  if (expected != actual) {
    fmt::println("FAILED '{}': mismatched wat output", tsPath.c_str());
    return TestResult::Failure;
  }
  BinaryenModuleDispose(ret.m);
  return TestResult::Success;
}

bool isAnyASCFlagsNotImplemented(nlohmann::json::array_t const &ascFlags, std::filesystem::path const &tsPath) {
  if (ascFlags.empty())
    return false;
  constexpr const char *allowedASCFlags[] = {
      "--exportStart _start", "--runtime incremental", "--initialMemory 2",
      "--exportRuntime",      "--bindings raw",        "--use Date=",
  };
  for (auto const &flag : ascFlags) {
    if (0 == std::count(std::begin(allowedASCFlags), std::end(allowedASCFlags), flag.get<std::string>())) {
      fmt::println("SKIP '{}': has not supported asc_flags '{}'", tsPath.c_str(), flag.get<std::string>());
      return true;
    }
  }
  return false;
}

bool shouldSkipTestCase(nlohmann::json const &configJson, std::filesystem::path const &tsPath) {
  constexpr const char *ascFlags = "asc_flags";
  constexpr const char *features = "features";
  if (configJson.contains(ascFlags)) {
    if (isAnyASCFlagsNotImplemented(configJson[ascFlags].get<nlohmann::json::array_t>(), tsPath))
      return true;
  }
  if (configJson.contains(features) && !configJson[features].get<nlohmann::json::array_t>().empty()) {
    fmt::println("SKIP '{}': has not supported features", tsPath.c_str());
    return true;
  }
  return false;
}

[[nodiscard]] TestResult run(std::filesystem::path const &tsPath) {
  try {
    std::filesystem::path const jsonPath = replaceExtension(tsPath, ".json");
    nlohmann::json const configJson =
        std::filesystem::exists(jsonPath) ? nlohmann::json::parse(readTextFile(jsonPath)) : nlohmann::json{};
    if (shouldSkipTestCase(configJson, tsPath))
      return TestResult::Skip;
    std::filesystem::path const expectedOutPath = replaceExtension(tsPath, ".wat");
    if (updateFlag.get())
      return runUpdate(configJson, tsPath, expectedOutPath);
    if (!std::filesystem::exists(expectedOutPath)) {
      return runCompilationErrorCase(configJson, tsPath);
    } else {
      return runSnapshotCase(configJson, tsPath, expectedOutPath);
    }
  } catch (std::exception const &e) {
    fmt::println("FAILED '{}': unkown error {}", tsPath.c_str(), e.what());
    return TestResult::Failure;
  }
}

void collectTestFilesImpl(std::vector<std::filesystem::path> &testFiles, std::filesystem::path const &folder) {
  for (std::filesystem::directory_entry const &entry : std::filesystem::directory_iterator{folder}) {
    if (entry.is_directory()) {
      // ignore all file inside node_modules
      if (entry.path().filename() == "node_modules")
        continue;
      collectTestFilesImpl(testFiles, entry.path());
    } else if (entry.is_regular_file()) {
      if (entry.path().extension() != ".ts")
        continue;
      // ignore .d.ts
      if (entry.path().stem().extension() == ".d")
        continue;
      testFiles.push_back(entry.path());
    }
  }
}

std::filesystem::path getTestFolder() {
  std::filesystem::path const folder = std::filesystem::path{__FILE__}.parent_path() / "compiler";
  return folder;
}

std::vector<std::filesystem::path> collectTestFiles(std::filesystem::path const &folder) {
  std::vector<std::filesystem::path> testFiles;
  collectTestFilesImpl(testFiles, folder);
  return testFiles;
}

void frontendTestMain(int argc, const char *argv[]) {
  frontend::init();
  Colors::setEnabled(false);

  std::atomic<size_t> numFailed = 0;
  std::atomic<size_t> numSkipped = 0;

  argparse::ArgumentParser program("FrontendTest", "git@" GIT_COMMIT);
  cli::init(cli::Category::OnlyForTest, program, argc, argv);

  std::filesystem::path const testFolder = getTestFolder();
  std::filesystem::current_path(testFolder);
  std::vector<std::filesystem::path> const testFiles = collectTestFiles(testFolder);

  size_t const numThreads = std::max(1U, std::thread::hardware_concurrency());
  fmt::println("using {} threads for test execution.", numThreads);
  std::vector<std::thread> executeThread{};
  executeThread.reserve(numThreads);
  auto const executor = [&testFiles, &numFailed, &numSkipped, numThreads](size_t currentThreadId) -> void {
    size_t const chunkSize = (testFiles.size() / numThreads) + 1U;
    size_t const start = chunkSize * currentThreadId;
    size_t const end = std::min(chunkSize * (currentThreadId + 1U), testFiles.size());
    for (size_t i = start; i < end; i++) {
      TestResult const testResult = run(testFiles[i]);
      switch (testResult) {
      case TestResult::Success:
        break;
      case TestResult::Failure:
        numFailed++;
        break;
      case TestResult::Skip:
        numSkipped++;
        break;
      }
    }
  };
  for (size_t threadId = 0; threadId < numThreads; threadId++) {
    executeThread.emplace_back(executor, threadId);
  }
  for (size_t threadId = 0; threadId < executeThread.size(); threadId++) {
    executeThread[threadId].join();
  }

  if (numFailed > 0U)
    throw std::runtime_error("test failed");
}

} // namespace
} // namespace warpo

int main(int argc, const char *argv[]) {
  try {
    warpo::frontendTestMain(argc, argv);
  } catch (std::exception const &e) {
    fmt::println("ERROR: {}", e.what());
    return 1;
  }
}
