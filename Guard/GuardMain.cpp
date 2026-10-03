// LiveRackGuard --pid <pid> --app <bundle path> --marker <running marker file>
//
// Spawned by LiveRack at launch. Waits for that process to exit; if it exits
// while the "running" marker still exists (crash, abort, kill -9), relaunches
// the app, which then restores the autosaved show with suspect plugins
// bypassed. Gives up after 3 relaunches within 60 seconds.

#include <sys/event.h>
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

extern char** environ;

static bool fileExists (const std::string& path)
{
    return access (path.c_str(), F_OK) == 0;
}

int main (int argc, char* argv[])
{
    pid_t pid = 0;
    std::string app, marker;

    for (int i = 1; i + 1 < argc; i += 2)
    {
        if (std::strcmp (argv[i], "--pid") == 0)    pid = (pid_t) std::atoi (argv[i + 1]);
        if (std::strcmp (argv[i], "--app") == 0)    app = argv[i + 1];
        if (std::strcmp (argv[i], "--marker") == 0) marker = argv[i + 1];
    }

    if (pid <= 0 || app.empty() || marker.empty())
        return 2;

    const int kq = kqueue();
    struct kevent change;
    EV_SET (&change, (uintptr_t) pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);

    if (kevent (kq, &change, 1, nullptr, 0, nullptr) < 0)
        return 1; // process already gone

    struct kevent event;
    while (kevent (kq, nullptr, 0, &event, 1, nullptr) < 1) {}

    usleep (200 * 1000); // let the app finish deleting its marker on a clean quit

    if (! fileExists (marker))
        return 0; // clean exit

    // Rate-limit relaunches to avoid a crash loop.
    const std::string history = marker + ".relaunches";
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

    const char* args[] = { "/usr/bin/open", "-n", app.c_str(), nullptr };
    pid_t child = 0;
    if (posix_spawn (&child, "/usr/bin/open", nullptr, nullptr, const_cast<char**> (args), environ) != 0)
        return 1;

    int status = 0;
    waitpid (child, &status, 0);
    return 0;
}
