/*
    SPDX-FileCopyrightText: 2010 Sebastian Doerner <sebastian@sebastian-doerner.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "checkoutdialog.h"
#include "gitwrapper.h"

#include <KConfigGroup>
#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QRadioButton>
#include <QString>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>

CheckoutDialog::CheckoutDialog(QWidget* parent):
    QDialog(parent, Qt::Dialog),
    m_userEditedNewBranchName(false)
{
    //branch/tag selection
    this->setWindowTitle(xi18nc("@title:window", "<application>Git</application> Checkout"));
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    QWidget *mainWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout;
    this->setLayout(mainLayout);
    mainLayout->addWidget(mainWidget);
    QPushButton *okButton = m_buttonBox->button(QDialogButtonBox::Ok);
    okButton->setDefault(true);
    okButton->setShortcut(Qt::CTRL | Qt::Key_Return);
    this->connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    this->connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    okButton->setText(i18nc("@action:button", "Checkout"));

    QWidget *boxWidget = new QWidget(this);
    QVBoxLayout *boxLayout = new QVBoxLayout(boxWidget);
    mainLayout->addWidget(boxWidget);

    m_branchSelectGroupBox = new QGroupBox(boxWidget);
    mainLayout->addWidget(m_branchSelectGroupBox);
    boxLayout->addWidget(m_branchSelectGroupBox);

    QGridLayout * gridLayout = new QGridLayout(m_branchSelectGroupBox);
    m_branchSelectGroupBox->setLayout(gridLayout);

    m_branchRadioButton = new QRadioButton(i18nc("@option:radio Git Checkout","Branch:"), m_branchSelectGroupBox);
    m_branchRadioButton->setChecked(true);
    gridLayout->addWidget(m_branchRadioButton,0,0);
    m_branchRadioButton->setFocus();
    m_branchComboBox = new QComboBox(m_branchSelectGroupBox);
    gridLayout->addWidget(m_branchComboBox,0,1);

    QRadioButton * tagRadioButton = new QRadioButton(i18nc("@option:radio Git Checkout", "Tag:"), m_branchSelectGroupBox);
    gridLayout->addWidget(tagRadioButton,1,0);
    m_tagComboBox = new QComboBox(m_branchSelectGroupBox);
    m_tagComboBox->setEnabled(false);
    gridLayout->addWidget(m_tagComboBox,1,1);

    //options
    QGroupBox * optionsGroupBox = new QGroupBox(boxWidget);
    mainLayout->addWidget(optionsGroupBox);
    boxLayout->addWidget(optionsGroupBox);
    optionsGroupBox->setTitle(i18nc("@title:group", "Options"));

    QGridLayout * optionsGridLayout = new QGridLayout(optionsGroupBox);
    optionsGroupBox->setLayout(optionsGridLayout);

    m_newBranchCheckBox = new QCheckBox(i18nc("@option:check", "Create New Branch: "), optionsGroupBox);
    m_newBranchCheckBox->setToolTip(i18nc("@info:tooltip", "Create a new branch based on a selected branch or tag."));
    optionsGridLayout->addWidget(m_newBranchCheckBox, 0,0);

    mainLayout->addWidget(m_buttonBox);

    m_newBranchName = new QLineEdit(optionsGroupBox);
    m_newBranchName->setMinimumWidth(150);
    m_newBranchName->setClearButtonEnabled(true);
    optionsGridLayout->addWidget(m_newBranchName, 0, 1);

    //initialize alternate color scheme for errors
    m_errorColors = m_newBranchName->palette();
    m_errorColors.setColor(QPalette::Normal, QPalette::Base, Qt::red);
    m_errorColors.setColor(QPalette::Inactive, QPalette::Base, Qt::red);

    m_forceCheckBox = new QCheckBox(i18nc("@option:check", "Force"), optionsGroupBox);
    m_forceCheckBox->setToolTip(i18nc("@info:tooltip", "Discard local changes."));
    optionsGridLayout->addWidget(m_forceCheckBox, 1,0);

    //get branch names
    GitWrapper * gitWrapper = GitWrapper::instance();
    int currentBranchIndex;
    const QStringList branches = gitWrapper->branches(&currentBranchIndex);

    m_branchComboBox->addItems(branches);
    if (currentBranchIndex == -1) {
        m_branchComboBox->insertItem(0, "(no branch)");
        m_branchComboBox->setCurrentIndex(0);
    } else {
        m_branchComboBox->setCurrentIndex(currentBranchIndex);
    }
    setDefaultNewBranchName(m_branchComboBox->currentText());

    //keep local branches to prevent creating an equally named new branch
    for (const QString& b : branches) {
        if (!b.startsWith(QLatin1String("remotes/"))) {
            //you CAN create local branches called "remotes/...", but since no sane person
            //would do that, we save the effort of another call to "git branch"
            m_branchNames.insert(b);
        }
    }

    //get tag names
    const QStringList tags = gitWrapper->tags();
    m_tagComboBox->addItems(tags);
    m_tagComboBox->setCurrentIndex(m_tagComboBox->count()-1);
    if (m_tagComboBox->count() == 0){
        tagRadioButton->setEnabled(false);
        const QString tooltip = i18nc("@info:tooltip", "There are no tags in this repository.");
        tagRadioButton->setToolTip(tooltip);
        m_tagComboBox->setToolTip(tooltip);
    }

    //signals
    connect(m_branchRadioButton, &QAbstractButton::toggled,
            this, &CheckoutDialog::branchRadioButtonToggled);
    connect(m_branchComboBox, SIGNAL(currentIndexChanged(QString)),
            this, SLOT(setDefaultNewBranchName(QString)));
    connect(m_branchComboBox, SIGNAL(currentIndexChanged(QString)),
            this, SLOT(setOkButtonState()));
    connect(m_tagComboBox, SIGNAL(currentIndexChanged(QString)),
            this, SLOT(setDefaultNewBranchName(QString)));
    connect(m_newBranchCheckBox, &QCheckBox::stateChanged,
             this, &CheckoutDialog::newBranchCheckBoxStateToggled);
    connect(m_newBranchName, &QLineEdit::textChanged,
            this, &CheckoutDialog::setOkButtonState);
    connect(m_newBranchName, &QLineEdit::textEdited,
            this, &CheckoutDialog::noteUserEditedNewBranchName);
    //set initial widget states
    newBranchCheckBoxStateToggled(Qt::Unchecked);
}

QString CheckoutDialog::checkoutIdentifier() const
{
    QString identifier = m_branchComboBox->isEnabled() ? m_branchComboBox->currentText() : m_tagComboBox->currentText();
    if (identifier.length()==0 || identifier.at(0)=='(') {
        identifier = "";
    }
    return identifier;
}

bool CheckoutDialog::force() const
{
    return m_forceCheckBox->isChecked();
}

QString CheckoutDialog::newBranchName() const
{
    if (m_newBranchCheckBox->isChecked()) {
        return m_newBranchName->text().trimmed();
    } else {
        return QString();
    }
}

void CheckoutDialog::branchRadioButtonToggled(bool checked)
{
    m_branchComboBox->setEnabled(checked);
    m_tagComboBox->setEnabled(!checked);
    setDefaultNewBranchName(checked ? m_branchComboBox->currentText() : m_tagComboBox->currentText());
    setOkButtonState();
}

void CheckoutDialog::newBranchCheckBoxStateToggled(int state)
{
    m_newBranchName->setEnabled(state == Qt::Checked);
    m_branchSelectGroupBox->setTitle(
        state == Qt::Checked ?
        i18nc("@title:group", "Branch Base") :
        i18nc("@title:group", "Checkout"));
    if (state == Qt::Checked) {
        m_newBranchName->setFocus(Qt::TabFocusReason);
    }
    setOkButtonState();
}

void CheckoutDialog::setOkButtonState()
{
    //default to enabled
    bool enableButton = true;
    bool newNameError = false;
    QPushButton *okButton = m_buttonBox->button(QDialogButtonBox::Ok);

    //------------disable on these conditions---------------//
    if (m_newBranchCheckBox->isChecked()) {
        const QString newBranchName = m_newBranchName->text().trimmed();
        if (newBranchName.isEmpty()){
            enableButton = false;
            newNameError = true;
            const QString tt = i18nc("@info:tooltip", "You must enter a valid name for the new branch first.");
            m_newBranchName->setToolTip(tt);
            okButton->setToolTip(tt);
        }
        if (m_branchNames.contains(newBranchName)) {
            enableButton = false;
            newNameError = true;
            const QString tt = i18nc("@info:tooltip", "A branch with the name '%1' already exists.", newBranchName);
            m_newBranchName->setToolTip(tt);
            okButton->setToolTip(tt);
        }
        if (newBranchName.contains(QRegExp("\\s"))) {
            enableButton = false;
            newNameError = true;
            const QString tt = i18nc("@info:tooltip", "Branch names may not contain any whitespace.");
            m_newBranchName->setToolTip(tt);
            okButton->setToolTip(tt);
        }
    } //if we create a new branch and no valid branch is selected we create one based on the currently checked out version
    else if (m_branchRadioButton->isChecked() && m_branchComboBox->currentText().at(0) == '('){
        enableButton = false;
        okButton->setToolTip(i18nc("@info:tooltip", "You must select a valid branch first."));
    }
    //------------------------------------------------------//

    setLineEditErrorModeActive(newNameError);
    okButton->setEnabled(enableButton);
    if (!newNameError) {
        m_newBranchName->setToolTip(QString());
    }
    if (enableButton) {
        okButton->setToolTip(QString());
    }
}

void CheckoutDialog::setDefaultNewBranchName(const QString& baseBranchName)
{
    if (!m_userEditedNewBranchName) {
        if (baseBranchName.startsWith('(')) {
            m_newBranchName->setText("");
        } else {
            m_newBranchName->setText(i18nc("@item:intext Prepended to the current branch name "
            "to get the default name for a newly created branch", "branch") + '_' + baseBranchName);
        }
    }
}

void CheckoutDialog::noteUserEditedNewBranchName()
{
    m_userEditedNewBranchName = true;
}

void CheckoutDialog::setLineEditErrorModeActive(bool active)
{
    m_newBranchName->setPalette(active ? m_errorColors : QPalette());
}

