#include <QApplication>
#include <QMessageBox>

#include <boost/program_options.hpp>

#include <components/files/configurationmanager.hpp>
#include <components/openmw-mp/IniSettings.hpp>

#include "MainWindow.hpp"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // tes3mp-ng change (major): TES3MP's config files are read with mwmp::IniSettings, because OpenMW 0.49
    // replaced the instance-based Settings::Manager
    Files::ConfigurationManager cfgMgr;
    MasterServerAddress master, defaultMaster;
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
        master = { settings.getString("address", "Master"), settings.getInt("port", "Master") };

        // tes3mp-ng addition: the master server players can return to from the Master Server dialog
        mwmp::IniSettings defaults;
        defaults.loadDefault(cfgMgr, "tes3mp-client");
        defaultMaster = { defaults.getString("address", "Master"), defaults.getInt("port", "Master") };
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(nullptr, QObject::tr("TES3MP-NG server browser"), QString::fromStdString(e.what()));
        return 1;
    }

    MainWindow d(cfgMgr.getUserConfigPath(), master, defaultMaster);

    d.show();
    return app.exec();
}
