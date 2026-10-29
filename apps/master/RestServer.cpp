#include "RestServer.hpp"

#include <iostream>
#include <mutex>
#include <sstream>

#include <RakSleep.h>

using namespace std;
using namespace chrono;

namespace
{
    constexpr size_t sMaxRequestSize = 8192;
    constexpr auto sRequestTimeout = 10s;

    string response(const string& code, const string& content, const string& type = "text/plain")
    {
        stringstream ss;
        ss << "HTTP/1.1 " << code << "\r\n";
        ss << "Content-Type: " << type << "\r\n";
        ss << "Access-Control-Allow-Origin: *\r\n";
        ss << "Connection: close\r\n";
        ss << "Content-Length: " << content.length() << "\r\n\r\n" << content;
        return ss.str();
    }

    string jsonString(const string& value)
    {
        string out = "\"";
        for (unsigned char c : value)
        {
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        char buffer[8];
                        snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                        out += buffer;
                    }
                    else
                        out += static_cast<char>(c);
            }
        }
        return out + "\"";
    }

    void serverToStream(stringstream& ss, const string& key, const MasterServer::SServer& server)
    {
        ss << jsonString(key) << ":{";
        ss << "\"modname\": " << jsonString(server.GetGameMode()) << ", ";
        ss << "\"passw\": " << (server.GetPassword() ? "true" : "false") << ", ";
        ss << "\"hostname\": " << jsonString(server.GetName()) << ", ";
        ss << "\"query_port\": " << 0 << ", ";
        ss << "\"last_update\": " << duration_cast<seconds>(steady_clock::now() - server.lastUpdate).count() << ", ";
        ss << "\"players\": " << server.GetPlayers() << ", ";
        ss << "\"version\": " << jsonString(server.GetVersion()) << ", ";
        ss << "\"max_players\": " << server.GetMaxPlayers();
        ss << "}";
    }
}

RestServer::RestServer(unsigned short port, MasterServer& masterServer)
    : mPort(port)
    , mMasterServer(masterServer)
    , mTcp(RakNet::TCPInterface::GetInstance())
{
}

RestServer::~RestServer()
{
    stop();
    RakNet::TCPInterface::DestroyInstance(mTcp);
}

bool RestServer::start()
{
    if (mRun)
        return true;
    if (!mTcp->Start(mPort, 64))
    {
        cout << "Could not start the HTTP API on port " << mPort << endl;
        return false;
    }
    cout << "HTTP API listening on port " << mPort << endl;
    mRun = true;
    mThread = std::thread(&RestServer::thread, this);
    return true;
}

void RestServer::stop()
{
    if (!mRun)
        return;
    mRun = false;
    if (mThread.joinable())
        mThread.join();
    mTcp->Stop();
}

void RestServer::thread()
{
    while (mRun)
    {
        bool idle = true;

        for (RakNet::SystemAddress address = mTcp->HasNewIncomingConnection();
             address != RakNet::UNASSIGNED_SYSTEM_ADDRESS; address = mTcp->HasNewIncomingConnection())
        {
            mConnections[address].opened = steady_clock::now();
            idle = false;
        }

        for (RakNet::SystemAddress address = mTcp->HasLostConnection();
             address != RakNet::UNASSIGNED_SYSTEM_ADDRESS; address = mTcp->HasLostConnection())
            mConnections.erase(address);

        for (RakNet::Packet* packet = mTcp->Receive(); packet != nullptr; packet = mTcp->Receive())
        {
            idle = false;
            Connection& connection = mConnections[packet->systemAddress];
            if (connection.opened == steady_clock::time_point())
                connection.opened = steady_clock::now();

            if (!connection.responded)
            {
                connection.request.append(reinterpret_cast<const char*>(packet->data), packet->length);

                string reply;
                if (connection.request.find("\r\n\r\n") != string::npos)
                    reply = respond(connection.request);
                else if (connection.request.size() > sMaxRequestSize)
                    reply = response("400 Bad Request", "bad request");

                if (!reply.empty())
                {
                    mTcp->Send(reply.data(), static_cast<unsigned>(reply.size()), packet->systemAddress, false);
                    connection.responded = true;
                    connection.request.clear();
                }
            }
            mTcp->DeallocatePacket(packet);
        }

        // Close connections once their response has been handed to the socket, and drop idle ones
        const auto now = steady_clock::now();
        for (auto it = mConnections.begin(); it != mConnections.end();)
        {
            const bool sent = it->second.responded && mTcp->GetOutgoingDataBufferSize(it->first) == 0;
            if (sent || now - it->second.opened > sRequestTimeout)
            {
                mTcp->CloseConnection(it->first);
                it = mConnections.erase(it);
            }
            else
                ++it;
        }

        if (idle)
            RakSleep(10);
    }
}

string RestServer::respond(const string& request)
{
    // Request line: METHOD PATH VERSION
    const size_t methodEnd = request.find(' ');
    const size_t pathEnd = methodEnd == string::npos ? string::npos : request.find(' ', methodEnd + 1);
    if (pathEnd == string::npos)
        return response("400 Bad Request", "bad request");

    const string method = request.substr(0, methodEnd);
    string path = request.substr(methodEnd + 1, pathEnd - methodEnd - 1);
    path = path.substr(0, path.find('?'));
    while (path.size() > 1 && path.back() == '/')
        path.pop_back();

    if (method != "GET")
        return response("405 Method Not Allowed", "method not allowed");

    static const string serversPath = "/api/servers";
    if (path == serversPath)
        return response("200 OK", serversJson(), "application/json");
    if (path == serversPath + "/info")
        return response("200 OK", infoJson(), "application/json");
    if (path.rfind(serversPath + "/", 0) == 0)
    {
        const string json = serverJson(path.substr(serversPath.size() + 1));
        if (!json.empty())
            return response("200 OK", json, "application/json");
        return response("404 Not Found", "not found");
    }
    return response("400 Bad Request", "bad request");
}

string RestServer::serversJson()
{
    lock_guard lock(mMasterServer.GetMutex());
    const MasterServer::ServerMap& servers = *mMasterServer.GetServers();

    stringstream ss;
    ss << "{\"list servers\":{";
    for (auto it = servers.begin(); it != servers.end(); ++it)
    {
        if (it != servers.begin())
            ss << ", ";
        serverToStream(ss, it->first.ToString(true, ':'), it->second);
    }
    ss << "}}";
    return ss.str();
}

string RestServer::serverJson(const string& address)
{
    const size_t colon = address.rfind(':');
    if (colon == string::npos || colon == 0 || colon + 1 >= address.size())
        return {};

    unsigned long port = 0;
    try
    {
        size_t used = 0;
        port = stoul(address.substr(colon + 1), &used);
        if (used != address.size() - colon - 1 || port > 65535)
            return {};
    }
    catch (const exception&)
    {
        return {};
    }

    RakNet::SystemAddress systemAddress;
    if (!systemAddress.FromStringExplicitPort(address.substr(0, colon).c_str(), static_cast<unsigned short>(port)))
        return {};

    lock_guard lock(mMasterServer.GetMutex());
    const MasterServer::ServerMap& servers = *mMasterServer.GetServers();
    auto it = servers.find(systemAddress);
    if (it == servers.end())
        return {};

    stringstream ss;
    ss << "{";
    serverToStream(ss, "server", it->second);
    ss << "}";
    return ss.str();
}

string RestServer::infoJson()
{
    lock_guard lock(mMasterServer.GetMutex());
    const MasterServer::ServerMap& servers = *mMasterServer.GetServers();

    unsigned players = 0;
    for (const auto& server : servers)
        players += server.second.GetPlayers();

    stringstream ss;
    ss << "{\"servers\": " << servers.size() << ", \"players\": " << players << "}";
    return ss.str();
}
