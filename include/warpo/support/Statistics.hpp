#pragma once

#include <chrono>

namespace warpo::support {

enum class PerfItemKind {
#define PERF_ITEM_KIND_TOP(name) name,
#define PERF_ITEM_KIND_CHILD(parent, name) parent##_##name,
#include "warpo/support/StatisticsKinds.def"
};

class PerfRAII {
  bool free_ = false;
  PerfItemKind item_;
  std::chrono::nanoseconds startTime_;

public:
  explicit PerfRAII(PerfItemKind item);
  ~PerfRAII();

  void release();
};

} // namespace warpo::support
