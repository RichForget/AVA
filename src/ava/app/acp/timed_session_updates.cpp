#include "sys.h"
#include "ava/app/acp/protocol.h"
#include "ava/app/acp/timed_session_updates.h"

#include <utility>

namespace ava::app::acp {
TimedSessionUpdates::TimedSessionUpdates(RuntimeSessionUpdateMapperOptions options, Sink sink)
    : mapper_(std::move(options)), sink_(std::move(sink)), worker_([this] { run(); })
{
}

TimedSessionUpdates::~TimedSessionUpdates()
{
  // Exceptional unwind must stop and join, never call a potentially throwing
  // consumer. Normal prompt completion explicitly calls finish to drain text.
  std::lock_guard lock(mutex_);
  stopping_ = true;
  wake_.notify_one();
}

ava::core::VoidResult TimedSessionUpdates::send(ava::core::Result<std::vector<std::string>> updates)
{
  if (failure_)
    return std::unexpected(*failure_);
  if (!updates)
    failure_ = std::move(updates.error());
  else
    for (auto const& update : *updates)
    {
      try
      {
        auto sent = sink_(update);
        if (!sent)
        {
          failure_ = std::move(sent.error());
          break;
        }
      }
      catch (...)
      {
        failure_ = protocol_error("ACP update callback failed");
        break;
      }
    }

  if (failure_)
    return std::unexpected(*failure_);
  return {};
}

ava::core::VoidResult TimedSessionUpdates::submit(ava::event::RuntimeEvent const& event)
{
  std::lock_guard lock(mutex_);
  if (stopping_)
    return std::unexpected(protocol_error("ACP update stream is closed"));
  auto result = send(mapper_.map_coalesced_and_encode(event));
  if (!result)
    return result;
  if (mapper_.has_pending_content())
  {
    if (!deadline_)
      deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
  }
  else
    deadline_.reset();
  wake_.notify_one();
  return {};
}

void TimedSessionUpdates::run()
{
  std::unique_lock lock(mutex_);
  try
  {
    while (!stopping_)
    {
      if (!deadline_)
      {
        wake_.wait(lock, [this] { return stopping_ || deadline_.has_value(); });
        continue;
      }
      auto const deadline = *deadline_;
      if (wake_.wait_until(lock, deadline, [this, deadline] { return stopping_ || !deadline_ || *deadline_ != deadline; }))
        continue;
      static_cast<void>(send(mapper_.flush_coalesced()));
      deadline_.reset();
    }
  }
  catch (...)
  {
    failure_ = protocol_error("ACP timed update callback failed");
  }
}

ava::core::VoidResult TimedSessionUpdates::finish()
{
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    wake_.notify_one();
  }
  if (worker_.joinable())
    worker_.join();
  std::lock_guard lock(mutex_);
  return send(mapper_.flush_coalesced());
}

}  // namespace ava::app::acp
