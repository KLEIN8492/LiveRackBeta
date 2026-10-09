#include "MacActivity.h"
#import <Foundation/Foundation.h>

namespace lr
{
LatencyCriticalActivity::LatencyCriticalActivity (const char* reason)
{
    const NSActivityOptions options = NSActivityLatencyCritical
                                    | NSActivityUserInitiated
                                    | NSActivityIdleSystemSleepDisabled;

    id activity = [[NSProcessInfo processInfo] beginActivityWithOptions: options
                                                                 reason: [NSString stringWithUTF8String: reason]];
    token = (void*) CFBridgingRetain (activity);
}

LatencyCriticalActivity::~LatencyCriticalActivity()
{
    if (token != nullptr)
        [[NSProcessInfo processInfo] endActivity: (id) CFBridgingRelease (token)];
}
} // namespace lr
