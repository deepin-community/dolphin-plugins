/*
    SPDX-FileCopyrightText: 2011 Vishesh Yadav <vishesh3y@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "servewrapper.h"
#include "hgwrapper.h"

#include <QTextCodec>

HgServeWrapper *HgServeWrapper::m_instance = nullptr;

HgServeWrapper::HgServeWrapper(QObject *parent) :
    QObject(parent)
{
}

HgServeWrapper::~HgServeWrapper()
{
    QMutableHashIterator<QString, ServerProcessType*> it(m_serverList);
    while (it.hasNext()) {
        it.next();
        ///terminate server if not terminated already
        if (it.value()->process.state() != QProcess::NotRunning) {
            it.value()->process.terminate();
        }
        it.value()->deleteLater();
        it.remove();
    }
}

HgServeWrapper *HgServeWrapper::instance()
{
    if (m_instance == nullptr) {
        m_instance = new HgServeWrapper;
    }
    return m_instance;
}

void HgServeWrapper::startServer(const QString &repoLocation, int portNumber)
{
    ServerProcessType *server = m_serverList.value(repoLocation, nullptr);
    if (server != nullptr) {
        m_serverList.remove(repoLocation);
        server->deleteLater();
    }
    server = new ServerProcessType;
    m_serverList.insert(repoLocation, server);
    server->port = portNumber;
    server->process.setWorkingDirectory(HgWrapper::instance()->getBaseDir());

    connect(&server->process, &QProcess::started,
            this, &HgServeWrapper::started);
    connect(&server->process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &HgServeWrapper::slotFinished);
    connect(server, &ServerProcessType::readyReadLine,
            this, &HgServeWrapper::readyReadLine);

    QStringList args;
    args << QLatin1String("-oL");
    args << QLatin1String("hg");
    args << QLatin1String("serve");
    args << QLatin1String("--port");
    args << QString::number(portNumber);
    server->process.start(QLatin1String("stdbuf"), args);
    Q_EMIT readyReadLine(repoLocation,
            i18n("## Starting Server ##"));
    Q_EMIT readyReadLine(repoLocation,
            QString("% hg serve --port %1").arg(portNumber));
}

void HgServeWrapper::stopServer(const QString &repoLocation)
{
    ServerProcessType *server = m_serverList.value(repoLocation, nullptr);
    if (server == nullptr) {
        return;
    }
    server->process.terminate();
}

bool HgServeWrapper::running(const QString &repoLocation)
{
    ServerProcessType *server = m_serverList.value(repoLocation, nullptr);
    if (server == nullptr) {
        return false;
    }
    return ( server->process.state() == QProcess::Running || 
             server->process.state() == QProcess::Starting);
}

void HgServeWrapper::slotFinished(int exitCode, QProcess::ExitStatus status)
{
    if (exitCode == 0 && status == QProcess::NormalExit) {
        Q_EMIT finished();
    }
    else {
        Q_EMIT error();
    }
}

QString HgServeWrapper::errorMessage(const QString &repoLocation)
{
    ServerProcessType *server = m_serverList.value(repoLocation, nullptr);
    if (server == nullptr) {
        return QString();
    }
    return QTextCodec::codecForLocale()->toUnicode(server->process.readAllStandardError());
}

bool HgServeWrapper::normalExit(const QString &repoLocation)
{
    ServerProcessType *server = m_serverList.value(repoLocation, nullptr);
    if (server == nullptr) {
        return true;
    }

    return (server->process.exitStatus() == QProcess::NormalExit &&
            server->process.exitCode() == 0);
}

void HgServeWrapper::cleanUnused()
{
    QMutableHashIterator<QString, ServerProcessType*> it(m_serverList);
    while (it.hasNext()) {
        it.next();
        if (it.value()->process.state() == QProcess::NotRunning) {
            it.value()->deleteLater();
            it.remove();
        }
    }
}


