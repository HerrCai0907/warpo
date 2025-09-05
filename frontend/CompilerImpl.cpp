#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fmt/base.h>
#include <fmt/format.h>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ASC/ASC.hpp"
#include "CompilerImpl.hpp"
#include "LinkedAPI.hpp"
#include "warpo/frontend/Compiler.hpp"
#include "warpo/support/Debug.hpp"
#include "warpo/support/FileSystem.hpp"
#include "warpo/support/Statistics.hpp"
#include "wasm-compiler/src/WasmModule/WasmModule.hpp"
#include "wasm-compiler/src/core/common/ILogger.hpp"
#include "wasm-compiler/src/core/common/NativeSymbol.hpp"
#include "wasm-compiler/src/utils/STDCompilerLogger.hpp"
#include "wasm-compiler/src/utils/StackTop.hpp"
#include "wasm.h"

#include "src/core/runtime/TrapException.hpp"

namespace warpo::frontend {

namespace {

const std::string libraryPrefix = "~lib/";
const std::string extension = ".ts";

enum WasmFFIBool : uint32_t { WASM_FALSE = 0, WASM_TRUE = 1 };

} // namespace
int32_t FrontendCompiler::allocString(std::string_view str) {
  // FIXME: convert utf8 to utf16 need library
  int32_t const ptr = m.callExportedFunctionWithName<1>(stackTop, "__new", static_cast<int32_t>(str.size() * 2U),
                                                        static_cast<int32_t>(2))[0]
                          .i32;
  m.callExportedFunctionWithName<1>(stackTop, "__pin", ptr);
  uint8_t *const stringBegin = m.getLinearMemoryRegion(static_cast<uint32_t>(ptr), str.size() * 2U);
  for (size_t i = 0; i < str.size(); i++) {
    stringBegin[i * 2U] = str[i];
  }
  return ptr;
}

void FrontendCompiler::parseFile(int32_t const program, std::optional<std::string> const &code, std::string_view path,
                                 IsEntry isEntry) {
  m.callExportedFunctionWithName<0>(stackTop, "__setArgumentsLength", 4U);
  if (code.has_value()) {
    m.callExportedFunctionWithName<0>(stackTop, "parse", program, allocString(code.value()), allocString(path),
                                      isEntry);
  } else {
    m.callExportedFunctionWithName<0>(stackTop, "parse", program, 0U, allocString(path), isEntry);
  }
}

std::string FrontendCompiler::getAsString(int32_t ptr) {
  uint8_t const *header = m.getLinearMemoryRegion(ptr - 20U, 20);
  uint32_t size = 0;
  std::memcpy(&size, header + 16, sizeof(size));
  uint8_t const *content = m.getLinearMemoryRegion(ptr, size);
  size /= 2U;
  std::stringstream ss{};
  for (uint32_t i = 0; i < size; ++i) {
    ss << content[i * 2U];
  }
  return std::move(ss).str();
};

using PackageResolveResult = std::optional<std::pair<std::string, std::optional<std::string>>>;

static PackageResolveResult getPackageName(std::string const &fileInternalPath) {
  static std::regex const libRegex{R"(^~lib/((?:@[^/]+/)?[^/]+)(?:/(.*?))?$)"};
  std::smatch match;
  if (!std::regex_match(fileInternalPath, match, libRegex))
    return std::nullopt;
  std::string const packageName = match[1].str();
  std::optional<std::string> const filePath =
      (match.size() > 2 && match[2].matched) ? std::optional<std::string>{match[2].str()} : std::nullopt;
  return std::pair<std::string, std::optional<std::string>>{packageName, filePath};
}

std::optional<std::filesystem::path> FrontendCompiler::findPackageRoot(std::filesystem::path const &sourceInternalPath,
                                                                       std::string const &packageName) {
  auto const it = packageRootMap_.find(packageName);
  if (it != packageRootMap_.end())
    return it->second;
  std::filesystem::path current;
  PackageResolveResult const sourcePackage = getPackageName(sourceInternalPath);
  if (sourcePackage.has_value()) {
    std::string const sourcePackageName = (*sourcePackage).first;
    assert(packageRootMap_.contains(sourcePackageName));
    std::filesystem::path packagePath = packageRootMap_.at(sourcePackageName);
    current = packagePath;
  } else {
    current = std::filesystem::absolute(sourceInternalPath).parent_path();
  }
  while (current != current.root_path()) {
    std::filesystem::path const target = current / "node_modules" / packageName;
    // fmt::println("[module resolve] try to resolving library '{}' in '{}'", packageName, target.c_str());
    if (std::filesystem::exists(target) && std::filesystem::is_directory(target)) {
      packageRootMap_[packageName] = target;
      if (support::isDebug("ModuleResolve"))
        fmt::println("[module resolve] resolve library '{}' in '{}'", packageName, target.c_str());
      return target;
    }
    current = current.parent_path();
  }
  return std::nullopt;
}

FrontendCompiler::Dependency FrontendCompiler::getDependencyForNodeModules(std::string const &nextFileInternalPath,
                                                                           int32_t program, int32_t nextFile) {
  if (support::isDebug("ModuleResolve"))
    fmt::println("[module resolve] get dependency for '{}'", nextFileInternalPath);
  int32_t const dependee = m.callExportedFunctionWithName<1>(stackTop, "getDependee", program, nextFile)[0].i32;
  std::string const dependeePath = getAsString(dependee);
  if (PackageResolveResult const package = getPackageName(nextFileInternalPath); package.has_value()) {
    auto const [packageName, filePath] = *package;
    std::optional<std::filesystem::path> const packageRoot = findPackageRoot(dependeePath, packageName);
    if (!packageRoot) {
      if (support::isDebug("ModuleResolve"))
        fmt::println("[module resolve] cannot find node_modules for package '{}'", packageName);
      return {std::nullopt, nextFileInternalPath + extension};
    }
    std::filesystem::path const plainPath = filePath.has_value() ? (*packageRoot / *filePath) : (*packageRoot);
    const std::string filePathWithExt = plainPath.string() + extension;
    if (std::filesystem::exists(filePathWithExt) && std::filesystem::is_regular_file(filePathWithExt)) {
      std::string const internalPath = libraryPrefix + packageName + "/" + *filePath + extension;
      if (support::isDebug("ModuleResolve"))
        fmt::println("[module resolve] resolve '{}' to '{}'", nextFileInternalPath, internalPath);
      return {readTextFile(filePathWithExt), internalPath};
    }
    const std::string indexPathWithExt = plainPath / ("index" + extension);
    if (std::filesystem::exists(indexPathWithExt) && std::filesystem::is_regular_file(indexPathWithExt)) {
      std::string const internalPath =
          libraryPrefix + packageName + (filePath.has_value() ? ("/" + *filePath) : "") + "/index" + extension;
      if (support::isDebug("ModuleResolve"))
        fmt::println("[module resolve] resolve '{}' to '{}'", nextFileInternalPath, internalPath);
      return {readTextFile(indexPathWithExt), internalPath};
    }
  }
  if (support::isDebug("ModuleResolve"))
    fmt::println("[module resolve] cannot find library '{}'", nextFileInternalPath);
  return {std::nullopt, nextFileInternalPath + extension};
}
FrontendCompiler::Dependency FrontendCompiler::getDependencyForUserCode(std::string const &nextFileInternalPath) {
  const std::string filePathWithExt = nextFileInternalPath + extension;
  if (std::filesystem::exists(filePathWithExt) && std::filesystem::is_regular_file(filePathWithExt)) {
    if (support::isDebug("ModuleResolve"))
      fmt::println("[module resolve] find user code '{}'", filePathWithExt);
    return {readTextFile(filePathWithExt), filePathWithExt};
  }
  const std::string indexPathWithExt = std::filesystem::path{nextFileInternalPath} / ("index" + extension);
  if (std::filesystem::exists(indexPathWithExt) && std::filesystem::is_regular_file(indexPathWithExt)) {
    return {readTextFile(indexPathWithExt), indexPathWithExt};
  }
  const std::string dFilePathWithExt = nextFileInternalPath + ".d" + extension;
  if (std::filesystem::exists(dFilePathWithExt) && std::filesystem::is_regular_file(dFilePathWithExt)) {
    return {readTextFile(dFilePathWithExt), filePathWithExt};
  }
  if (support::isDebug("ModuleResolve"))
    fmt::println("[module resolve] cannot find library '{}'", nextFileInternalPath);
  return {std::nullopt, nextFileInternalPath + extension};
}
FrontendCompiler::Dependency FrontendCompiler::getDependency(std::string const &nextFileInternalPath, int32_t program,
                                                             int32_t nextFile) {
  support::PerformanceStatisticRange const r{support::PerfItemKind::CompilationHIR_Parsing_DepsResolve};
  if (nextFileInternalPath.starts_with(libraryPrefix)) {
    std::string const plainName = nextFileInternalPath.substr(libraryPrefix.size());
    if (embed_library_sources.contains(plainName)) {
      return {embed_library_sources.at(plainName), libraryPrefix + plainName + extension};
    }
    std::string const indexName = plainName + "/index";
    if (embed_library_sources.contains(indexName)) {
      return {embed_library_sources.at(indexName), libraryPrefix + indexName + extension};
    }
    return getDependencyForNodeModules(nextFileInternalPath, program, nextFile);
  }
  return getDependencyForUserCode(nextFileInternalPath);
}

std::vector<FrontendCompiler::Dependency> FrontendCompiler::getAllDependencies(int32_t const program) {
  std::vector<Dependency> dependencies{};
  while (true) {
    int32_t const nextFile = m.callExportedFunctionWithName<1U>(stackTop, "nextFile", program)[0].i32;
    if (nextFile == 0U) {
      break;
    }
    std::string const nextFileInternalPath = getAsString(nextFile);
    dependencies.push_back(getDependency(nextFileInternalPath, program, nextFile));
  }
  return dependencies;
}

bool FrontendCompiler::checkDiag(int32_t const program, bool useColorfulDiagMessage) {
  size_t errorCount = 0;
  while (true) {
    int32_t const diag = m.callExportedFunctionWithName<1>(stackTop, "nextDiagnostic", program)[0].i32;
    if (diag == 0)
      break;
    bool const isError = static_cast<bool>(m.callExportedFunctionWithName<1>(stackTop, "isError", diag)[0].i32);
    if (isError)
      errorCount++;
    m.callExportedFunctionWithName<0>(stackTop, "__setArgumentsLength", 3U);
    int32_t const diagStrOffset =
        m.callExportedFunctionWithName<1>(stackTop, "formatDiagnostic", diag,
                                          useColorfulDiagMessage ? WasmFFIBool::WASM_TRUE : WasmFFIBool::WASM_FALSE,
                                          WasmFFIBool::WASM_TRUE)[0]
            .i32;
    errorMessage_ += getAsString(diagStrOffset) + "\n\n";
  }
  errorCount_ += errorCount;
  return errorCount > 0;
}

FrontendCompiler::~FrontendCompiler() {
  for (void *p : allocedPtrs_) {
    std::free(p);
  }
}

FrontendCompiler::FrontendCompiler(Config const &config)
    : logger(), m{logger}, stackTop(static_cast<uint8_t const *>(vb::getStackTop())) {
  m.setStacktraceRecordCount(32U);
  m.setContext(this);

  if (config.ascWasmPath) [[unlikely]] {
    std::string const wasmBytes = readBinaryFile(*config.ascWasmPath);
    m.initFromBytecode(
        vb::Span<const uint8_t>{reinterpret_cast<uint8_t const *>(wasmBytes.data()), wasmBytes.size()},
        vb::Span<vb::NativeSymbol const>{warpo::frontend::linkedAPI.data(), warpo::frontend::linkedAPI.size()});
  } else {
    support::PerformanceStatisticRange const range{support::PerfItemKind::CompilationHIR_PrepareWASMModule};
    static vb::WasmModule::CompileResult const embedJitCode = m.compile(
        vb::Span<const uint8_t>{embed_asc_wasm.data(), embed_asc_wasm.size()},
        vb::Span<vb::NativeSymbol const>{warpo::frontend::linkedAPI.data(), warpo::frontend::linkedAPI.size()});
    m.initFromCompiledBinary(
        vb::Span<uint8_t const>{embedJitCode.getModule().data(), embedJitCode.getModule().size()},
        vb::Span<vb::NativeSymbol const>{},
        vb::Span<uint8_t const>{embedJitCode.getDebugSymbol().data(), embedJitCode.getDebugSymbol().size()});
  }
}

warpo::frontend::Result FrontendCompiler::compile(std::vector<std::string> const &entryFilePaths,
                                                  Config const &config) {
  try {
    support::PerformanceStatisticRange initStat{support::PerfItemKind::CompilationHIR_Init};
    m.start(stackTop);
    m.callExportedFunctionWithName<0>(stackTop, "_initialize");

    int32_t const option = m.callExportedFunctionWithName<1>(stackTop, "newOptions")[0].i32;
    m.callExportedFunctionWithName<1>(stackTop, "__pin", option);

    enum class RuntimeKind : uint32_t { Incremental = 2 };
    constexpr uint32_t stackSize = 32768U;
    m.callExportedFunctionWithName<0>(stackTop, "setRuntime", option, RuntimeKind::Incremental);
    m.callExportedFunctionWithName<0>(stackTop, "setStackSize", option, stackSize);

    enum class SetFeatureOn : uint32_t { OFF = 0, ON = 1 };
    uint32_t const asFeatureFlags = config.features.toASFeaturesFlags();
    m.callExportedFunctionWithName<0>(stackTop, "setFeature", option, ~asFeatureFlags, SetFeatureOn::OFF);
    m.callExportedFunctionWithName<0>(stackTop, "setFeature", option, asFeatureFlags, SetFeatureOn::ON);

    m.callExportedFunctionWithName<0>(stackTop, "setExportTable", option,
                                      config.exportTable ? WasmFFIBool::WASM_TRUE : WasmFFIBool::WASM_FALSE);
    m.callExportedFunctionWithName<0>(stackTop, "setExportRuntime", option,
                                      config.exportRuntime ? WasmFFIBool::WASM_TRUE : WasmFFIBool::WASM_FALSE);
    if (config.exportStart.has_value())
      m.callExportedFunctionWithName<0>(stackTop, "setExportStart", option, allocString(*config.exportStart));
    m.callExportedFunctionWithName<0>(stackTop, "setDebugInfo", option, WasmFFIBool::WASM_TRUE);
    m.callExportedFunctionWithName<0>(stackTop, "setSourceMap", option,
                                      config.emitDebugLine ? WasmFFIBool::WASM_TRUE : WasmFFIBool::WASM_FALSE);
    if (config.initialMemory.has_value())
      m.callExportedFunctionWithName<0>(stackTop, "setInitialMemory", option, *config.initialMemory);

    for (auto const &[useName, useValue] : config.uses)
      m.callExportedFunctionWithName<0>(stackTop, "addGlobalAlias", option, allocString(useName),
                                        allocString(useValue));
    m.callExportedFunctionWithName<0>(stackTop, "setOptimizeLevelHints", option, config.optimizationLevel,
                                      config.shrinkLevel);

    int32_t const program = m.callExportedFunctionWithName<1>(stackTop, "newProgram", option)[0].i32;
    m.callExportedFunctionWithName<1>(stackTop, "__pin", program);
    initStat.release();

    support::PerformanceStatisticRange parseStat{support::PerfItemKind::CompilationHIR_Parsing};
    support::PerformanceStatisticRange parseLibStat{support::PerfItemKind::CompilationHIR_Parsing_BuiltinLib};
    for (auto const &[libName, libSource] : warpo::frontend::embed_library_sources) {
      // in sub-directory: imported on demand
      if (libName.find('/') != std::string::npos)
        continue;
      parseFile(program, libSource, libraryPrefix + libName + extension, IsEntry::NO);
    }
    parseFile(program, warpo::frontend::embed_library_sources.at("rt/index-incremental"),
              libraryPrefix + "rt/index-incremental" + extension, IsEntry::NO);
    parseLibStat.release();

    for (std::string const &filePath : entryFilePaths) {
      std::string const relativeFilePath = std::filesystem::relative(filePath).string();
      parseFile(program, readTextFile(filePath), relativeFilePath, IsEntry::YES);
    }
    while (true) {
      std::vector<Dependency> const deps = getAllDependencies(program);
      if (deps.empty())
        break;
      for (auto const &[text, path] : deps) {
        if (support::isDebug("ModuleResolve")) {
          if (!embed_library_sources.contains(path))
            fmt::println("[module resolve] parse '{}'", path);
        }
        parseFile(program, text, path, IsEntry::NO);
      }
    }
    if (checkDiag(program, config.useColorfulDiagMessage))
      return {.m = nullptr, .errorMessage = errorMessage_};
    parseStat.release();

    support::PerformanceStatisticRange compileStat{support::PerfItemKind::CompilationHIR_Compilation};
    m.callExportedFunctionWithName<0>(stackTop, "initializeProgram", program);
    int32_t const compiled = m.callExportedFunctionWithName<1>(stackTop, "compile", program)[0].i32;
    if (checkDiag(program, config.useColorfulDiagMessage))
      return {.m = nullptr, .errorMessage = errorMessage_};
    wasm::Module *binaryen_module = reinterpret_cast<wasm::Module *>(
        m.callExportedFunctionWithName<1>(stackTop, "getBinaryenModuleRef", compiled)[0].i64);
    compileStat.release();
    return {.m = binaryen_module, .errorMessage = std::nullopt};
  } catch (vb::TrapException const &e) {
    logger << "Error: " << e.what() << vb::endStatement;
    m.printStacktrace(logger);
  } catch (std::exception const &e) {
    logger << "Error: " << e.what() << vb::endStatement;
  }
  return {.m = nullptr, .errorMessage = "AS wasm execution failed"};
}

} // namespace warpo::frontend
