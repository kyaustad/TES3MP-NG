#ifndef NEWRESTAPI_RESTSERVER_HPP
#define NEWRESTAPI_RESTSERVER_HPP

#include <atomic>
#include <chrono>
#include <map>
#include <string>
#include <thread>

#include <TCPInterface.h>

#include "MasterServer.hpp"

/*
    tes3mp-ng change (major): a read-only HTTP API on CrabNet's TCPInterface, replacing Simple-Web-Server, which
    needs Boost.Asio APIs that current Boost versions no longer have. The routes for servers older than 0.6
    (POST and PUT on /api/servers) are gone, because those servers announce themselves through RakNet

    GET /api/servers                  all servers
    GET /api/servers/<address>:<port> one server
    GET /api/servers/info             numbers of servers and players
*/
class RestServer
{
public:
    RestServer(unsigned short port, MasterServer& masterServer);
    ~RestServer();

    bool start();
    void stop();

private:
    struct Connection
    {
        std::string request;
        std::chrono::steady_clock::time_point opened;
        bool responded = false;
    };

    void thread();
    std::string respond(const std::string& request);
    std::string serversJson();
    std::string serverJson(const std::string& address);
    std::string infoJson();

    unsigned short mPort;
    MasterServer& mMasterServer;
    RakNet::TCPInterface* mTcp;
    std::map<RakNet::SystemAddress, Connection> mConnections;
    std::thread mThread;
    std::atomic<bool> mRun{ false };
};

#endif // NEWRESTAPI_RESTSERVER_HPP
