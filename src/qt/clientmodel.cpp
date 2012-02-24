#include "clientmodel.h"
#include "guiconstants.h"
#include "optionsmodel.h"
#include "addresstablemodel.h"
#include "transactiontablemodel.h"

#include <coinChain/Node.h>

#include <QTimer>
#include <QDateTime>

ClientModel::ClientModel(const Node& node, OptionsModel *optionsModel, QObject *parent) :
    QObject(parent), node(node), optionsModel(optionsModel),
    cachedNumConnections(0), cachedNumBlocks(0)
{
    // Until signal notifications is built into the bitcoin core,
    //  simply update everything after polling using a timer.
    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    timer->start(MODEL_UPDATE_DELAY);

    numBlocksAtStartup = -1;
}

int ClientModel::getNumConnections() const
{
    return node.getConnectionCount();
}

int ClientModel::getNumBlocks() const
{
    return node.blockChain().getBestHeight();
}

int ClientModel::getNumBlocksAtStartup()
{
    if (numBlocksAtStartup == -1) numBlocksAtStartup = getNumBlocks();
    return numBlocksAtStartup;
}

QDateTime ClientModel::getLastBlockDate() const
{
    return QDateTime::fromTime_t(node.blockChain().getBestIndex()->GetBlockTime());
}

void ClientModel::update()
{
    int newNumConnections = getNumConnections();
    int newNumBlocks = getNumBlocks();

    if(cachedNumConnections != newNumConnections)
        emit numConnectionsChanged(newNumConnections);
    if(cachedNumBlocks != newNumBlocks)
        emit numBlocksChanged(newNumBlocks);

    cachedNumConnections = newNumConnections;
    cachedNumBlocks = newNumBlocks;
}

bool ClientModel::isTestNet() const
{
    return node.blockChain().chain().dataDirSuffix() == "bitcoin/testnet";
}

bool ClientModel::inInitialBlockDownload() const
{
    return node.blockChain().isInitialBlockDownload();
}

int ClientModel::getNumBlocksOfPeers() const
{
    return node.getPeerMedianNumBlocks();
}

QString ClientModel::getStatusBarWarnings() const
{
//    return QString::fromStdString(GetWarnings("statusbar"));
    return QString::fromStdString("");
}

OptionsModel *ClientModel::getOptionsModel()
{
    return optionsModel;
}

QString ClientModel::formatFullVersion() const
{
    return QString::fromStdString(FormatFullVersion());
}
