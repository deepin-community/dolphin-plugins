/*
    SPDX-FileCopyrightText: 2011 Vishesh Yadav <vishesh3y@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "syncdialogbase.h"
#include "hgconfig.h"
#include "pathselector.h"
#include "fileviewhgpluginsettings.h"

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStringList>
#include <QTextCodec>
#include <QHeaderView>
#include <QLabel>
#include <QCheckBox>
#include <QGroupBox>
#include <QProgressBar>
#include <KTextEdit>
#include <KLocalizedString>
#include <KMessageBox>

HgSyncBaseDialog::HgSyncBaseDialog(DialogType dialogType, QWidget *parent):
    DialogBase(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, parent),
    m_haveChanges(false),
    m_terminated(false),
    m_dialogType(dialogType)
{
    m_hgw = HgWrapper::instance();
}

void HgSyncBaseDialog::setup()
{
    createChangesGroup();
    readBigSize();
    setupUI();
    
    connect(m_changesButton, &QAbstractButton::clicked,
            this, &HgSyncBaseDialog::slotGetChanges);
    connect(&m_process, &QProcess::stateChanged,
            this, &HgSyncBaseDialog::slotUpdateBusy);
    connect(&m_main_process, &QProcess::stateChanged,
            this, &HgSyncBaseDialog::slotUpdateBusy);
    connect(&m_main_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &HgSyncBaseDialog::slotOperationComplete);
    connect(&m_main_process, &QProcess::errorOccurred,
            this, &HgSyncBaseDialog::slotOperationError);
    connect(&m_process, &QProcess::errorOccurred,
            this, &HgSyncBaseDialog::slotOperationError);
    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &HgSyncBaseDialog::slotChangesProcessComplete);
    connect(this, &QDialog::finished, this, &HgSyncBaseDialog::slotWriteBigSize);
}

void HgSyncBaseDialog::createOptionGroup()
{
    setOptions();
    QVBoxLayout *layout = new QVBoxLayout;

    for (QCheckBox *cb : qAsConst(m_options)) {
        layout->addWidget(cb);
    }

    m_optionGroup = new QGroupBox(this);
    m_optionGroup->setLayout(layout);
    m_optionGroup->setVisible(false);
}

void HgSyncBaseDialog::setupUI()
{
    // top url bar
    m_pathSelector = new HgPathSelector;

    // changes button
    //FIXME not very good idea. Bad HACK 
    if (m_dialogType == PullDialog) {
        m_changesButton = new QPushButton(i18nc("@label:button",
                "Show Incoming Changes"));
    }
    else {
        m_changesButton = new QPushButton(i18nc("@label:button",
                "Show Outgoing Changes"));
    }
    m_changesButton->setSizePolicy(QSizePolicy::Fixed,
            QSizePolicy::Fixed);
    m_changesButton->setCheckable(true);

    // dialog's main widget
    QWidget *widget = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout;
    lay->addWidget(m_pathSelector);

    // changes
    m_changesGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    lay->addWidget(m_changesGroup);

    // bottom bar
    QHBoxLayout *bottomLayout = new QHBoxLayout;
    m_statusProg = new QProgressBar;
    m_statusProg->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    bottomLayout->addWidget(m_changesButton, Qt::AlignLeft);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_statusProg);
    
    //
    lay->addLayout(bottomLayout);
    widget->setLayout(lay);

    createOptionGroup();

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(widget);
    mainLayout->addWidget(m_optionGroup);

    // bottom button box with OK/Cancel and Options buttons
    okButton()->setText(xi18nc("@action:button",
                               m_dialogType == PullDialog ? "Pull" : "Push"));
    okButton()->setIcon(QIcon::fromTheme(
                               m_dialogType == PullDialog ? "git-pull" : "git-push"));
    m_optionsButton = new QPushButton(buttonBox());
    m_optionsButton->setIcon(QIcon::fromTheme("help-about"));
    switchOptionsButton(true);
    buttonBox()->addButton(m_optionsButton, QDialogButtonBox::ResetRole);
    layout()->insertLayout(0, mainLayout);

    connect(m_optionsButton, &QAbstractButton::clicked, this, &HgSyncBaseDialog::slotOptionsButtonClick);
}

void HgSyncBaseDialog::slotGetChanges()
{
    if (m_haveChanges) {
        m_changesGroup->setVisible(!m_changesGroup->isVisible());
        m_changesButton->setChecked(m_changesGroup->isVisible());
        if (m_changesGroup->isVisible()) {
            loadBigSize();
        }
        else {
            loadSmallSize();
        }
        return;
    }
    if (m_process.state() == QProcess::Running) {
        return;
    }

    QStringList args;
    getHgChangesArguments(args);
    m_process.setWorkingDirectory(m_hgw->getBaseDir());
    m_process.start(QLatin1String("hg"), args);
}


void HgSyncBaseDialog::slotChangesProcessComplete(int exitCode, QProcess::ExitStatus status)
{

    if (exitCode != 0 || status != QProcess::NormalExit) {
        QString message = QTextCodec::codecForLocale()->toUnicode(m_process.readAllStandardError());
        if (message.isEmpty()) {
            message = i18nc("@message", "No changes found!");
        }
        KMessageBox::error(this, message);
        return;
    }

    char buffer[512];

    /**
     * unwantedRead boolean to ensure that unwanted information messages
     * by mercurial are filtered out.
     *  eg. Comparing with /path/to/repository
     *      Searching for changes
     */
    bool unwantedRead = false;

    /**
     * hasChanges boolean checks whether there are any changes to be sent 
     * or received and invoke noChangesMessage() if its false
     */
    bool hasChanges = false;

    while (m_process.readLine(buffer, sizeof(buffer)) > 0) {
        QString line(QTextCodec::codecForLocale()->toUnicode(buffer));
        if (unwantedRead ) {
            line.remove(QLatin1String("Commit: "));
            parseUpdateChanges(line.trimmed());
            hasChanges = true;;
        }
        else if (line.startsWith(QLatin1String("Commit: "))) {
            unwantedRead = true;
            line.remove(QLatin1String("Commit: "));
            parseUpdateChanges(line.trimmed());
            hasChanges = true;
        }
    }

    if (!hasChanges) {
        noChangesMessage();
    }

    m_changesGroup->setVisible(true);
    m_changesButton->setChecked(true);
    loadBigSize();
    m_haveChanges = true; 
    Q_EMIT changeListAvailable();
}

void HgSyncBaseDialog::slotChangesProcessError()
{
    qDebug() << "Cant get changes";
    KMessageBox::error(this, i18n("Error!"));
}

void HgSyncBaseDialog::loadSmallSize()
{
    m_bigSize = size();
    resize(m_smallSize);
    adjustSize();
    updateGeometry();
}

void HgSyncBaseDialog::loadBigSize()
{
    m_smallSize = size();
    resize(m_bigSize);
}

void HgSyncBaseDialog::slotWriteBigSize()
{
    if (m_changesGroup->isVisible()) {
        m_bigSize = size();
    }
    writeBigSize();
}

void HgSyncBaseDialog::done(int r)
{
    if (r == QDialog::Accepted) {
        if (m_main_process.state() == QProcess::Running ||
                m_main_process.state() == QProcess::Starting) {
            qDebug() << "HgWrapper already busy";
            return;
        }

        QStringList args;
        QString command = (m_dialogType==PullDialog)?"pull":"push";
        args << command;
        args << m_pathSelector->remote();
        appendOptionArguments(args);

        m_terminated = false;
        
        m_main_process.setWorkingDirectory(m_hgw->getBaseDir());
        m_main_process.start(QLatin1String("hg"), args);
    }
    else {
        if (m_process.state() == QProcess::Running || 
            m_process.state() == QProcess::Starting ||
            m_main_process.state() == QProcess::Running ||
            m_main_process.state() == QProcess::Starting) 
        {
            if (m_process.state() == QProcess::Running ||
                    m_process.state() == QProcess::Starting) {
                m_process.terminate();
            }
            if (m_main_process.state() == QProcess::Running ||
                     m_main_process.state() == QProcess::Starting) {
                qDebug() << "terminating pull/push process";
                m_terminated = true;
                m_main_process.terminate();
            }
        }
        else {
            QDialog::done(r);
        }
    }
}

void HgSyncBaseDialog::slotOperationComplete(int exitCode, QProcess::ExitStatus status)
{
    if (exitCode == 0 && status == QProcess::NormalExit) {
        QDialog::done(QDialog::Accepted);
    }
    else {
        if (!m_terminated) {
            KMessageBox::error(this, i18n("Error!"));
        }
    }
}

void HgSyncBaseDialog::slotOperationError()
{
    KMessageBox::error(this, i18n("Error!"));
}

void HgSyncBaseDialog::slotUpdateBusy(QProcess::ProcessState state)
{
    if (state == QProcess::Running || state == QProcess::Starting) {
        m_statusProg->setRange(0, 0);
        m_changesButton->setEnabled(false);
        m_changesButton->setChecked(true);
        okButton()->setDisabled(true);
    }
    else {
        m_statusProg->setRange(0, 100);
        m_changesButton->setEnabled(true);
        okButton()->setDisabled(false);
    }
    m_statusProg->repaint();
    QApplication::processEvents();
}

void HgSyncBaseDialog::slotOptionsButtonClick()
{
  if (m_optionsButton->text().contains(">>")) {
    switchOptionsButton(false);
    m_optionGroup->setVisible(true);
  }
  else {
    switchOptionsButton(true);
    m_optionGroup->setVisible(false);
  }
}

void HgSyncBaseDialog::switchOptionsButton(bool switchOn)
{
  m_optionsButton->setText(xi18nc("@action:button", "Options") +
                      (switchOn ? " >>" : " <<"));
}



