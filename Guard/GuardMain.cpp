// LiveRackGuard --pid <pid> --app <what to relaunch> --marker <running marker file>
//
// Spawned by LiveRack at launch. Waits for that process to exit; if it exits
// while the "running" marker still exists (crash, abort, kill -9), relaunches
// the app (the .app bundle on macOS, the executable elsewhere), which then
// restores the autosaved show with suspect plugins bypassed. Gives up after 3
// relaunches within 60 seconds. No dependencies: it must work when the app is
// in any state.

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

#if defined (_WIN32)
 #define NOMINMAX
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
 using Path = std::wstring;
 #define LR_TEXT(s) L##s
#else
 #include <sys/stat.h>
 #include <sys/wait.h>
 #include <signal.h>
 #include <spawn.h>
 #include <unistd.h>
 #include <cerrno>
 #if defined (__APPLE__)
  #include <sys/event.h>
 #else
  #include <poll.h>
  #include <sys/syscall.h>
 #endif
 extern char** environ;
 using Path = std::string;
 #define LR_TEXT(s) s
#endif

namespace
{
// ---- per OS -----------------------------------------------------------------

#if defined (_WIN32)

bool fileExists (const Path& path)
{
    return GetFileAttributesW (path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void sleepMs (int ms) { Sleep ((DWORD) ms); }

// Blocks until the process exits. False if it's already gone (or not ours).
bool waitForExit (long pid)
{
    HANDLE process = OpenProcess (SYNCHRONIZE, FALSE, (DWORD) pid);
    if (process == nullptr)
        return false;

    WaitForSingleObject (process, INFINITE);
    CloseHandle (process);
    return true;
}

bool relaunch (const Path& app)
{
    std::wstring cmd = L"\"" + app + L"\"";
    STARTUPINFOW startup {};
    startup.cb = sizeof (startup);
    PROCESS_INFORMATION info {};

    if (! CreateProcessW (app.c_str(), cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &startup, &info))
        return false;

    CloseHandle (info.hThread);
    CloseHandle (info.hProcess);
    return true;
}

#else

bool fileExists (const Path& path)
{
    struct stat st;
    return stat (path.c_str(), &st) == 0;
}

void sleepMs (int ms) { usleep ((useconds_t) ms * 1000); }

bool waitForExit (long pid)
{
   #if defined (__APPLE__)
    const int kq = kqueue();
    struct kevent change;
    EV_SET (&change, (uintptr_t) pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);

    if (kevent (kq, &change, 1, nullptr, 0, nullptr) < 0)
        return false; // process already gone

    struct kevent event;
    while (kevent (kq, nullptr, 0, &event, 1, nullptr) < 1) {}
    return true;
   #else
    // pidfd (Linux 5.3+): readable when the process exits, no polling and
    // no pid-reuse race. Older kernels: check every 100 ms.
   #if defined (SYS_pidfd_open)
    const int fd = (int) syscall (SYS_pidfd_open, (pid_t) pid, 0);
    if (fd >= 0)
    {
        pollfd p { fd, POLLIN, 0 };
        while (poll (&p, 1, -1) < 0 && errno == EINTR) {}
        close (fd);
        return true;
    }
   #endif
    if (kill ((pid_t) pid, 0) != 0 && errno == ESRCH)
        return false;

    while (! (kill ((pid_t) pid, 0) != 0 && errno == ESRCH))
        sleepMs (100);
    return true;
   #endif
}

bool relaunch (const Path& app)
{
   #if defined (__APPLE__)
    const char* args[] = { "/usr/bin/open", "-n", app.c_str(), nullptr };
    const char* exe = "/usr/bin/open";
   #else
    const char* args[] = { app.c_str(), nullptr };
    const char* exe = app.c_str();
   #endif

    posix_spawnattr_t attr;
    posix_spawnattr_init (&attr);
    posix_spawnattr_setflags (&attr, POSIX_SPAWN_SETSID); // the app gets its own session

    pid_t child = 0;
    const bool ok = posix_spawn (&child, exe, nullptr, &attr, const_cast<char**> (args), environ) == 0;
    posix_spawnattr_destroy (&attr);

   #if defined (__APPLE__)
    if (ok)
        waitpid (child, nullptr, 0); // `open` returns once the app is launched
   #endif
    return ok;
}

#endif

// ---- shared -----------------------------------------------------------------

int run (long pid, const Path& app, const Path& marker)
{
    if (pid <= 0 || app.empty() || marker.empty())
        return 2;

    if (! waitForExit (pid))
        return 1;

    sleepMs (200); // let the app finish deleting its marker on a clean quit

    if (! fileExists (marker))
        return 0; // clean exit

    // Rate-limit relaunches to avoid a crash loop.
    const Path history = marker + LR_TEXT (".relaunches");
    const time_t now = std::time (nullptr);
    std::vector<time_t> recent;
    {
        std::ifstream in (history);
        time_t t;
        while (in >> t)
            if (now - t < 60)
                recent.push_back (t);
    }

    if (recent.size() >= 3)
    {
        std::fprintf (stderr, "LiveRackGuard: too many crashes, not relaunching\n");
        return 1;
    }

    recent.push_back (now);
    {
        std::ofstream out (history, std::ios::trunc);
        for (auto t : recent)
            out << t << "\n";
    }

    return relaunch (app) ? 0 : 1;
}
} // namespace

#if defined (_WIN32)
int wmain (int argc, wchar_t* argv[])
{
    long pid = 0;
    Path app, marker;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::wstring key = argv[i];
        if (key == L"--pid")    pid = std::wcstol (argv[i + 1], nullptr, 10);
        if (key == L"--app")    app = argv[i + 1];
        if (key == L"--marker") marker = argv[i + 1];
    }
    return run (pid, app, marker);
}
#else
int main (int argc, char* argv[])
{
    long pid = 0;
    Path app, marker;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string key = argv[i];
        if (key == "--pid")    pid = std::atol (argv[i + 1]);
        if (key == "--app")    app = argv[i + 1];
        if (key == "--marker") marker = argv[i + 1];
    }
    return run (pid, app, marker);
}
#endif
