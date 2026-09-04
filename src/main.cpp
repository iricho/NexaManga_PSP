#include "App.hpp"
#include "BootDiagnostics.hpp"
#include "Log.hpp"
#include "Platform.hpp"

#include <cstdio>

#ifdef __PSP__
#include <pspkernel.h>

// Current PSPSDK treats any negative heap size as "largest free block";
// the separately declared threshold is the amount kept outside newlib.
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(1024);
#endif

int main(int argc, char* argv[]) {
    Platform::captureEarlyMemoryProfile();
    BootDiagnostics::initialize(argc > 0 ? argv[0] : nullptr);
    Log::initializePersistentLog(BootDiagnostics::logPath());
    BootDiagnostics::stage(1, "main entered");
    BootDiagnostics::holdIfRequested(1);

    App app;
    BootDiagnostics::stage(2, "App constructed");
    BootDiagnostics::holdIfRequested(2);

    if (!app.init()) {
        std::fprintf(stderr, "NexaManga PSP initialization failed: %s\n", app.lastError().c_str());
        BootDiagnostics::fatal(app.lastError().c_str());
        return 1;
    }

    return app.run();
}
