#include <argparse/argparse.hpp>
#include <binaryen/src/binaryen-c.h>
#include <cstdlib>
#include <cstring>
#include <fmt/base.h>
#include <fmt/format.h>
#include <fstream>

#include "warpo/frontend/Compiler.hpp"
#include "warpo/support/Opt.hpp"

static warpo::cli::Opt<std::string> outputPath{
    "-t",
    "--text",
    [](argparse::Argument &arg) -> void { arg.help("output text file").required(); },
};

int main(int argc, const char *argv[]) {
  using namespace warpo;

  frontend::init();

  argparse::ArgumentParser program("warpo_compiler", "git@" GIT_COMMIT);
  cli::init(program, argc, argv);

  BinaryenModuleRef const m = frontend::compile();
  if (m == nullptr)
    return -1;

  char *const wasmText = BinaryenModuleAllocateAndWriteText(m);
  std::ofstream watOf{outputPath.get(), std::ios::out};
  watOf << wasmText;
  std::free(wasmText);
}
