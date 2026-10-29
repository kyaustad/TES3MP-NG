#include <QApplication>
#include <QMessageBox>

#include <boost/program_options.hpp>

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
    Files::ConfigurationManager cfgMgr;
    std::string addr;
    int port = 0;
    try
    {
        // tes3mp-ng change (minor): follow openmw.cfg's config folder, like the client, so that portable installs
        // keep their own tes3mp-client.cfg and favorites
        boost::program_options::variables_map variables;
        boost::program_options::options_description desc;
        Files::ConfigurationManager::addCommonOptions(desc);
        cfgMgr.readConfiguration(variables, desc, true);

        mwmp::IniSettings settings;
        settings.loadDefaultAndUser(cfgMgr, "tes3mp-client");
        addr = settings.getString("address", "Master");
        port = settings.getInt("port", "Master");
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(nullptr, QObject::tr("TES3MP-NG server browser"), QString::fromStdString(e.what()));
        return 1;
    }

    // Is this an attempt to connect to the official master server at the old port? If so,
    // redirect it to the correct port for the currently used fork of RakNet
    if (Misc::StringUtils::ciEqual(addr, "master.tes3mp.com") && port == 25560)
        port = 25561;

    QueryClient::Get().SetServer(addr, static_cast<unsigned short>(port));
    MainWindow d(cfgMgr.getUserConfigPath());

    d.show();
    return app.exec();
}
