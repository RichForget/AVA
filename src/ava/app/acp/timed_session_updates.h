#pragma once
#include "ava/debug/print_members_on.h"
#include "ava/app/acp/session_update.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace ava::app::acp {
// One prompt owns one sleeping worker. The mutex orders mapping, timed flushes,
// and sends; finish joins before the prompt response or mapper lifetime ends.
class TimedSessionUpdates
{
 public:
  using Sink = std::function<ava::core::VoidResult(std::string const&)>;
  TimedSessionUpdates(RuntimeSessionUpdateMapperOptions options, Sink sink);
  ~TimedSessionUpdates();
  [[nodiscard]] ava::core::VoidResult submit(ava::event::RuntimeEvent const& event);
  [[nodiscard]] ava::core::VoidResult finish();
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
 private:
  ava::core::VoidResult send(ava::core::Result<std::vector<std::string>> updates);
  void run();
  RuntimeSessionUpdateMapper mapper_;
  Sink sink_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  std::optional<ava::core::Error> failure_;
  bool stopping_ = false;
  std::jthread worker_;
};

}  // namespace ava::app::acp
