#include "InflightLog.h"

namespace lr
{
namespace
{
constexpr size_t fileSize = (size_t) InflightLog::recordSize * InflightLog::maxRecords;
}

InflightLog::~InflightLog() { close(); }

bool InflightLog::open (const juce::File& file)
{
    close();
    file.getParentDirectory().createDirectory();

    // Fresh, zeroed file of the full size, then mapped read-write. Writes go
    // to the OS's copy of the file, so they survive this process crashing.
    juce::HeapBlock<char> zeros (fileSize, true);
    if (! file.replaceWithData (zeros.get(), fileSize))
        return false;

    mapping = std::make_unique<juce::MemoryMappedFile> (file, juce::MemoryMappedFile::readWrite);
    if (mapping->getData() == nullptr || mapping->getSize() < fileSize)
    {
        close();
        return false;
    }

    data = static_cast<char*> (mapping->getData());
    used.assign (maxRecords, false);
    return true;
}

void InflightLog::close()
{
    mapping.reset();
    data = nullptr;
    used.clear();
}

int InflightLog::allocateRecord (const juce::String& slotId)
{
    if (data == nullptr)
        return -1;

    for (int i = 0; i < maxRecords; ++i)
    {
        if (used[(size_t) i])
            continue;

        used[(size_t) i] = true;
        char* rec = data + i * recordSize;
        std::memset (rec, 0, recordSize);
        slotId.copyToUTF8 (rec, recordSize - 1);
        return i;
    }

    return -1;
}

void InflightLog::releaseRecord (int record)
{
    if (data == nullptr || record < 0 || record >= maxRecords)
        return;

    used[(size_t) record] = false;
    std::memset (data + record * recordSize, 0, recordSize);
}

std::vector<juce::String> InflightLog::readSuspects (const juce::File& file)
{
    std::vector<juce::String> suspects;
    juce::MemoryBlock block;

    if (! file.loadFileAsData (block))
        return suspects;

    const auto* bytes = static_cast<const char*> (block.getData());
    const int records = (int) (block.getSize() / recordSize);

    for (int i = 0; i < records; ++i)
    {
        const char* rec = bytes + i * recordSize;
        if (rec[recordSize - 1] != 0 && rec[0] != 0)
            suspects.push_back (juce::String::fromUTF8 (rec, (int) strnlen (rec, recordSize - 1)));
    }

    return suspects;
}

} // namespace lr
