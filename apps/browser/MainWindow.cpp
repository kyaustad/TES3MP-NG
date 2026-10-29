#include "MainWindow.hpp"
#include "QueryHelper.hpp"
#include "PingHelper.hpp"
#include "ServerInfoDialog.hpp"
#include <qdebug.h>
#include <QInputDialog>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QJsonDocument>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <components/misc/strings/algorithm.hpp>
#include <components/openmw-mp/IniSettings.hpp>
#include <apps/browser/netutils/QueryClient.hpp>


using namespace Process;
using namespace std;

MainWindow::MainWindow(const std::filesystem::path &userConfigPath, const MasterServerAddress &master,
    const MasterServerAddress &defaultMaster, QWidget *parent)
    : mUserConfigPath(userConfigPath)
    , mDefaultMaster(defaultMaster)
{
    setupUi(this);
    setMasterServer(master);

    mGameInvoker = new ProcessInvoker();

    browser = new ServerModel;
    favorites = new ServerModel;
    proxyModel = new MySortFilterProxyModel(this);
    proxyModel->setSourceModel(browser);
    tblServerBrowser->setModel(proxyModel);
    tblFavorites->setModel(proxyModel);

    // Remove Favorites tab while it remains broken
    tabWidget->removeTab(1);

    tblServerBrowser->hideColumn(ServerData::ADDR);
    tblFavorites->hideColumn(ServerData::ADDR);

    PingHelper::Get().SetModel((ServerModel*)proxyModel->sourceModel());
    queryHelper = new QueryHelper(proxyModel->sourceModel());
    connect(queryHelper, &QueryHelper::started, [this](){actionRefresh->setEnabled(false); actionMasterServer->setEnabled(false);});
    connect(queryHelper, &QueryHelper::finished, [this](){actionRefresh->setEnabled(true); actionMasterServer->setEnabled(true);});

    connect(tabWidget, SIGNAL(currentChanged(int)), this, SLOT(tabSwitched(int)));
    connect(actionAdd, SIGNAL(triggered(bool)), this, SLOT(addServer()));
    connect(actionAdd_by_IP, SIGNAL(triggered(bool)), this, SLOT(addServerByIP()));
    connect(actionDelete, SIGNAL(triggered(bool)), this, SLOT(deleteServer()));
    connect(actionRefresh, SIGNAL(triggered(bool)), queryHelper, SLOT(refresh()));
    connect(actionPlay, SIGNAL(triggered(bool)), this, SLOT(play()));
    // Queued, so that pressing the button through accessibility tools returns before the modal dialog opens
    connect(actionMasterServer, &QAction::triggered, this, &MainWindow::changeMasterServer, Qt::QueuedConnection);
    connect(tblServerBrowser, SIGNAL(clicked(QModelIndex)), this, SLOT(serverSelected()));
    connect(tblFavorites, SIGNAL(clicked(QModelIndex)), this, SLOT(serverSelected()));
    connect(tblFavorites, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(play()));
    connect(tblServerBrowser, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(play()));
    connect(cBoxNotFull, SIGNAL(toggled(bool)), this, SLOT(notFullSwitch(bool)));
    connect(cBoxWithPlayers, SIGNAL(toggled(bool)), this, SLOT(havePlayersSwitch(bool)));
    connect(cBBoxWOPass, SIGNAL(toggled(bool)), this, SLOT(noPasswordSwitch(bool)));
    connect(comboLatency, SIGNAL(currentIndexChanged(int)), this, SLOT(maxLatencyChanged(int)));
    connect(leGamemode, SIGNAL(textChanged(const QString &)), this, SLOT(gamemodeChanged(const QString &)));
    loadFavorites();
    queryHelper->refresh();
}

MainWindow::~MainWindow()
{
    delete mGameInvoker;
}

void MainWindow::addServerAndUpdate(const QString &addr)
{
    favorites->insertRow(0);
    QModelIndex mi = favorites->index(0, ServerData::ADDR);
    favorites->setData(mi, addr, Qt::EditRole);
    //NetController::get()->updateInfo(favorites, mi);
    //QueryClient::Update(RakNet::SystemAddress())
    /*auto data = QueryClient::Get().Query();
    if (data.empty())
       return;
    transform(data.begin(), data.end(), back_inserter());*/
}

void MainWindow::addServer()
{
    int id = tblServerBrowser->selectionModel()->currentIndex().row();

    if (id >= 0)
    {
        int sourceId = proxyModel->mapToSource(proxyModel->index(id, ServerData::ADDR)).row();
        favorites->myData.push_back(browser->myData[sourceId]);
    }
}

void MainWindow::addServerByIP()
{
    bool ok;
    QString text = QInputDialog::getText(this, tr("Add Server by address"), tr("Address:"), QLineEdit::Normal, "", &ok);
    if (ok && !text.isEmpty())
        addServerAndUpdate(text);
}

void MainWindow::deleteServer()
{
    if (tabWidget->currentIndex() != 1)
        return;
    int id = tblFavorites->selectionModel()->currentIndex().row();
    if (id >= 0)
    {
        int sourceId = proxyModel->mapToSource(proxyModel->index(id, ServerData::ADDR)).row();
        favorites->removeRow(sourceId);
        if (favorites->myData.isEmpty())
        {
            actionPlay->setEnabled(false);
            actionDelete->setEnabled(false);
        }
    }
}

void MainWindow::play()
{
    QTableView *curTable = tabWidget->currentIndex() ? tblFavorites : tblServerBrowser;
    int id = curTable->selectionModel()->currentIndex().row();
    if (id < 0)
        return;


    ServerModel *sm = ((ServerModel*)proxyModel->sourceModel());

    int sourceId = proxyModel->mapToSource(proxyModel->index(id, ServerData::ADDR)).row();
    ServerInfoDialog infoDialog(sm->myData[sourceId].addr, this);

    if (!infoDialog.exec())
        return;

    if (!infoDialog.isUpdated())
        return;

    QStringList arguments;
    arguments.append(QLatin1String("--connect=") + sm->myData[sourceId].addr.toLatin1());

    if (sm->myData[sourceId].GetPassword() == 1)
    {
        bool ok;
        QString passw = QInputDialog::getText(this, tr("Connecting to: ") + sm->myData[sourceId].addr, tr("Password: "),
                                              QLineEdit::Password, "", &ok);
        if (!ok)
            return;
        arguments.append(QLatin1String("--password=") + passw.toLatin1());
    }

    // tes3mp-ng change (minor): the multiplayer client is built as openmw, like OpenMW's own executable
    if (mGameInvoker->startProcess(QLatin1String("openmw"), arguments, true))
        return qApp->quit();
}

void MainWindow::tabSwitched(int index)
{
    if (index == 0)
    {
        proxyModel->setSourceModel(browser);
        actionDelete->setEnabled(false);
    }
    else
    {
        proxyModel->setSourceModel(favorites);
    }
    actionPlay->setEnabled(false);
    actionAdd->setEnabled(false);
}

void MainWindow::serverSelected()
{
    actionPlay->setEnabled(true);
    if (tabWidget->currentIndex() == 0)
        actionAdd->setEnabled(true);
    if (tabWidget->currentIndex() == 1)
        actionDelete->setEnabled(true);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QString cfgPath = QString::fromStdString((mUserConfigPath / "favorites.dat").string());

    QJsonArray saveData;
    for (auto server : favorites->myData)
        saveData.push_back(server.addr);

    QFile file(cfgPath);

    if (!file.open(QIODevice::WriteOnly))
    {
        qDebug() << "Cannot save " << cfgPath;
        return;
    }

    file.write(QJsonDocument(saveData).toJson());
    file.close();
}


void MainWindow::loadFavorites()
{
    QString cfgPath = QString::fromStdString((mUserConfigPath / "favorites.dat").string());

    QFile file(cfgPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qDebug() << "Cannot open " << cfgPath;
        return;
    }

    QJsonDocument jsonDoc(QJsonDocument::fromJson(file.readAll()));

    for (auto server : jsonDoc.array())
        addServerAndUpdate(server.toString());

    file.close();
}

void MainWindow::notFullSwitch(bool state)
{
    proxyModel->filterFullServer(state);
}

void MainWindow::havePlayersSwitch(bool state)
{
    proxyModel->filterEmptyServers(state);
}

void MainWindow::noPasswordSwitch(bool state)
{
    proxyModel->filterPassworded(state);
}

void MainWindow::maxLatencyChanged(int index)
{
    int maxLatency = index * 50;
    proxyModel->pingLessThan(maxLatency);

}

void MainWindow::gamemodeChanged(const QString &text)
{
    proxyModel->setFilterFixedString(text);
    proxyModel->setFilterKeyColumn(ServerData::MODNAME);
}

/*
    Start of tes3mp-ng addition

    Let players choose the master server whose list the browser shows, and save it in their tes3mp-client.cfg
*/
void MainWindow::setMasterServer(const MasterServerAddress &master)
{
    mMaster = master;

    // Is this an attempt to connect to the official master server at the old port? If so,
    // redirect it to the correct port for the currently used fork of RakNet
    int port = master.port;
    if (Misc::StringUtils::ciEqual(master.address, "master.tes3mp.com") && port == 25560)
        port = 25561;

    QueryClient::Get().SetServer(master.address, static_cast<unsigned short>(port));
    setWindowTitle(tr("TES3MP-NG Server Browser") + QString(" - %1:%2")
        .arg(QString::fromStdString(master.address)).arg(master.port));
}

void MainWindow::changeMasterServer()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Master Server"));

    auto *address = new QLineEdit(QString::fromStdString(mMaster.address), &dialog);
    address->setMinimumWidth(260);
    auto *port = new QSpinBox(&dialog);
    port->setRange(1, 65535);
    port->setValue(mMaster.port);

    auto *form = new QFormLayout;
    form->addRow(tr("Address:"), address);
    form->addRow(tr("Port:"), port);

    auto *note = new QLabel(tr("The server list comes from this master server. Default: %1:%2")
        .arg(QString::fromStdString(mDefaultMaster.address)).arg(mDefaultMaster.port), &dialog);
    note->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
        | QDialogButtonBox::RestoreDefaults, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, [&]() {
        address->setText(QString::fromStdString(mDefaultMaster.address));
        port->setValue(mDefaultMaster.port);
    });

    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const std::string newAddress = address->text().trimmed().toStdString();
    if (newAddress.empty() || newAddress.find_first_of(" \t") != std::string::npos)
    {
        QMessageBox::warning(this, tr("Master Server"),
            tr("Enter the master server's address, such as master.example.com"));
        return;
    }
    const MasterServerAddress master{ newAddress, port->value() };

    // Save it in this install's tes3mp-client.cfg, keeping the file's other settings and comments
    const std::filesystem::path userConfig = mUserConfigPath / "tes3mp-client.cfg";
    try
    {
        mwmp::IniSettings settings;
        if (std::filesystem::exists(userConfig))
            settings.load(userConfig, false);
        settings.set("address", "Master", master.address);
        settings.set("port", "Master", std::to_string(master.port));
        settings.save(userConfig);
    }
    catch (const std::exception &e)
    {
        QMessageBox::warning(this, tr("Master Server"), tr("Could not save %1: %2")
            .arg(QString::fromStdString(userConfig.string()), QString::fromStdString(e.what())));
        return;
    }

    setMasterServer(master);
    queryHelper->refresh();
}
/*
    End of tes3mp-ng addition
*/
