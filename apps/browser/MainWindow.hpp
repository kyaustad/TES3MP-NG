#ifndef NEWLAUNCHER_MAIN_HPP
#define NEWLAUNCHER_MAIN_HPP


#include "ui_Main.h"
#include "ServerModel.hpp"
#include "MySortFilterProxyModel.hpp"
#include <components/process/processinvoker.hpp>

#include <filesystem>
#include <string>

// tes3mp-ng addition: the master server that lists servers, set in tes3mp-client.cfg's [Master] section
struct MasterServerAddress
{
    std::string address;
    int port = 0;
};

class QueryHelper;

class MainWindow : public QMainWindow,  private Ui::MainWindow
{
    Q_OBJECT
public:
    MainWindow(const std::filesystem::path &userConfigPath, const MasterServerAddress &master,
        const MasterServerAddress &defaultMaster, QWidget *parent = nullptr);
    ~MainWindow() override;
protected:
    void closeEvent(QCloseEvent * event) Q_DECL_OVERRIDE;
    void addServerAndUpdate(const QString &addr);
protected slots:
    void tabSwitched(int index);
    void addServer();
    void addServerByIP();
    void deleteServer();
    void play();
    void serverSelected();
    void notFullSwitch(bool state);
    void havePlayersSwitch(bool state);
    void noPasswordSwitch(bool state);
    void maxLatencyChanged(int index);
    void gamemodeChanged(const QString &text);
    void changeMasterServer();
private:
    std::filesystem::path mUserConfigPath; // tes3mp-ng addition: where favorites.dat and tes3mp-client.cfg are kept
    MasterServerAddress mMaster, mDefaultMaster;
    void setMasterServer(const MasterServerAddress &master);
    QueryHelper *queryHelper;
    Process::ProcessInvoker *mGameInvoker;
    ServerModel *browser, *favorites;
    MySortFilterProxyModel *proxyModel;
    void loadFavorites();
};


#endif //NEWLAUNCHER_MAIN_HPP
