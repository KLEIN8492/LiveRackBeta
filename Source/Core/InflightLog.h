#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace lr
{
// A small memory-mapped file with one record per plugin slot. The audio
// thread sets a slot's flag just before processBlock and clears it after.
// Because the mapping is shared with the file, the flags survive a crash:
// on the next launch any record still flagged names the plugin that was
// running when the app died.
class InflightLog
{
public:
    static constexpr int recordSize = 48; // 46 chars of slot id, NUL, flag
    static constexpr int maxRecords = 2048;

    InflightLog() = default;
    ~InflightLog();

    // Creates/zeroes the file and maps it. Read old suspects *before* this.
    bool open (const juce::File&);
    void close();

    // Message thread.
    int allocateRecord (const juce::String& slotId);
    void releaseRecord (int record);

    // Audio thread: a plain store into mapped memory.
    void mark (int record, bool inProcess) noexcept
    {
        if (data != nullptr && record >= 0 && record < maxRecords)
            reinterpret_cast<volatile char*> (data)[record * recordSize + recordSize - 1] = inProcess ? 1 : 0;
    }

    // Slot ids whose flag is set in the given file.
    static std::vector<juce::String> readSuspects (const juce::File&);

private:
    char* data = nullptr;
    int fd = -1;
    std::vector<bool> used;
};

} // namespace lr
