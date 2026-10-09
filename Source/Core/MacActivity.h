#pragma once

#include <memory>

namespace lr
{
// Tells macOS this process is doing latency-critical, user-initiated work
// (NSActivityLatencyCritical | NSActivityUserInitiated, system sleep disabled):
// exempt from App Nap and timer coalescing, no idle system sleep mid-show.
// Held for as long as the object lives.
class LatencyCriticalActivity
{
public:
    explicit LatencyCriticalActivity (const char* reason);
    ~LatencyCriticalActivity();

private:
    void* token = nullptr; // retained id<NSObject>
};
} // namespace lr
