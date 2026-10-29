#include <QApplication>
#include <QMessageBox>

#include <components/files/configurationmanager.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/openmw-mp/IniSettings.hpp>

#include <apps/browser/netutils/QueryClient.hpp>
#include "MainWindow.hpp"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // tes3mp-ng change (major): TES3MP's config files are read with mwmp::IniSettings, because OpenMW 0.49
    // replaced the instance-based Settings::Manager
    std::string addr;
    int port = 0;
    try
    {
        Files::ConfigurationManager cfgMgr;
        mwmp::IniSettings settings;
        settings.loadDefaultAndUser(cfgMgr, "tes3mp-client");
        addr = settings.getString("address", "Master");
        port = settings.getInt("port", "Master");
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(nullptr, QObject::tr("TES3MP server browser"), QString::fromStdString(e.what()));
        return 1;
    }

    // Is this an attempt to connect to the official master server at the old port? If so,
    // redirect it to the correct port for the currently used fork of RakNet
    if (Misc::StringUtils::ciEqual(addr, "master.tes3mp.com") && port == 25560)
        port = 25561;

    QueryClient::Get().SetServer(addr, static_cast<unsigned short>(port));
    MainWindow d;

    d.show();
    return app.exec();
}
