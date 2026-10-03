#include "InflightLog.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

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

    fd = ::open (file.getFullPathName().toRawUTF8(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return false;

    if (::ftruncate (fd, (off_t) fileSize) != 0)
    {
        close();
        return false;
    }

    void* mapped = ::mmap (nullptr, fileSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED)
    {
        close();
        return false;
    }

    data = static_cast<char*> (mapped);
    std::memset (data, 0, fileSize);
    used.assign (maxRecords, false);
    return true;
}

void InflightLog::close()
{
    if (data != nullptr)
        ::munmap (data, fileSize);
    if (fd >= 0)
        ::close (fd);

    data = nullptr;
    fd = -1;
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
