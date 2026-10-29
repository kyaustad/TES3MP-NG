#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include <RakSleep.h>

#include "MasterServer.hpp"
#include "RestServer.hpp"

using namespace std;

namespace
{
    atomic<bool> run{ true };

    void onExit(int /*sig*/)
    {
        run = false;
    }

    // tes3mp-ng addition: the ports can be set on the command line
    bool readPort(const char* arg, unsigned short& port)
    {
        char* end = nullptr;
        const unsigned long value = strtoul(arg, &end, 10);
        if (end == arg || *end != '\0' || value == 0 || value > 65535)
            return false;
        port = static_cast<unsigned short>(value);
        return true;
    }
}

int main(int argc, char* argv[])
{
    unsigned short masterPort = 25560;
    unsigned short httpPort = 8080;

    if (argc > 3 || (argc > 1 && !readPort(argv[1], masterPort)) || (argc > 2 && !readPort(argv[2], httpPort)))
    {
        cout << "Usage: " << argv[0] << " [master server port (25560)] [HTTP API port (8080)]" << endl;
        return 1;
    }

    signal(SIGINT, onExit);
    signal(SIGTERM, onExit);

    auto masterServer = make_unique<MasterServer>(2000, masterPort);
    auto restServer = make_unique<RestServer>(httpPort, *masterServer);

    cout << "Master server port " << masterPort << endl;
    masterServer->Start();
    restServer->start();

    while (run && masterServer->isRunning())
        RakSleep(100);

    restServer->stop();
    masterServer->Stop(true);
    return 0;
}
