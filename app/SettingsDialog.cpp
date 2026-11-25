#include "SettingsDialog.h"

#include "AppInfo.h"
#include "AppSettings.h"
#include "paint_schema.hpp"

#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(const QString& databasePath, const QHash<QString, double>& weights, int resultCount,
                               QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Options"));
    m_tabs = new QTabWidget(this);

    // ---------------------------------------------------------------- General
    auto* general = new QWidget(m_tabs);
    auto* form = new QFormLayout(general);

    m_path = new QLineEdit(QDir::toNativeSeparators(databasePath), general);
    m_path->setMinimumWidth(380);
    auto* browse = new QPushButton(tr("Browse…"), general);
    auto* defaultPath = new QPushButton(tr("Default"), general);
    defaultPath->setToolTip(QDir::toNativeSeparators(AppSettings::defaultDatabasePath()));
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browse);
    pathRow->addWidget(defaultPath);
    form->addRow(tr("Database CSV file:"), pathRow);

    auto* pathNote = new QLabel(tr("The database is loaded when the application starts. Files inside the "
                                   "application folder are remembered relative to it."),
                                general);
    pathNote->setWordWrap(true);
    form->addRow(QString(), pathNote);

    m_resultCount = new QSpinBox(general);
    m_resultCount->setRange(1, AppSettings::kMaxResultCount);
    m_resultCount->setValue(resultCount);
    form->addRow(tr("Default number of results:"), m_resultCount);

    auto* iniNote = new QLabel(tr("Settings file: %1").arg(QDir::toNativeSeparators(AppSettings::iniPath())), general);
    iniNote->setTextInteractionFlags(Qt::TextSelectableByMouse);
    iniNote->setWordWrap(true);
    form->addRow(QString(), iniNote);

    connect(browse, &QPushButton::clicked, this, &SettingsDialog::browseForDatabase);
    connect(defaultPath, &QPushButton::clicked, this,
            [this] { m_path->setText(QDir::toNativeSeparators(AppSettings::defaultDatabasePath())); });

    m_tabs->addTab(general, tr("General"));

    // ---------------------------------------------------------------- Default weights
    auto* weightsPage = new QWidget(m_tabs);
    auto* weightsLayout = new QVBoxLayout(weightsPage);
    auto* intro = new QLabel(tr("Default importance of each attribute in the match score. The main window starts "
                                "with these values and can change them for a single search. 0 means the "
                                "attribute is not scored (its min/max and Required filters still apply)."),
                             weightsPage);
    intro->setWordWrap(true);
    weightsLayout->addWidget(intro);

    auto* scroll = new QScrollArea(weightsPage);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget(scroll);
    auto* grid = new QGridLayout(inner);
    grid->setColumnStretch(0, 1);

    int row = 0;
    std::string group;
    for (const auto& def : paintdb::attributeDefs()) {
        if (def.group != group) {
            group = def.group;
            auto* header = new QLabel(QStringLiteral("<b>%1</b>").arg(toQ(group).toHtmlEscaped()), inner);
            header->setContentsMargins(0, row == 0 ? 0 : 8, 0, 0);
            grid->addWidget(header, row++, 0, 1, 3);
        }
        const QString key = toQ(def.key);
        auto* label = new QLabel(toQ(def.label), inner);
        label->setToolTip(toQ(def.hint));
        auto* box = new QDoubleSpinBox(inner);
        box->setDecimals(1);
        box->setRange(0.0, 10.0);
        box->setSingleStep(0.5);
        box->setValue(weights.value(key, def.defaultWeight()));
        auto* factory = new QLabel(tr("factory: %1").arg(def.defaultWeight()), inner);
        factory->setEnabled(false);
        grid->addWidget(label, row, 0);
        grid->addWidget(box, row, 1);
        grid->addWidget(factory, row, 2);
        m_weightBoxes.insert(key, box);
        ++row;
    }
    grid->setRowStretch(row, 1);
    scroll->setWidget(inner);
    weightsLayout->addWidget(scroll, 1);

    auto* restore = new QPushButton(tr("Restore Factory Weights"), weightsPage);
    connect(restore, &QPushButton::clicked, this, &SettingsDialog::restoreFactoryWeights);
    auto* restoreRow = new QHBoxLayout;
    restoreRow->addStretch(1);
    restoreRow->addWidget(restore);
    weightsLayout->addLayout(restoreRow);

    m_tabs->addTab(weightsPage, tr("Default Weights"));

    // ---------------------------------------------------------------- buttons
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(buttons);
    resize(620, 560);
}

QString SettingsDialog::databasePath() const {
    return QDir::cleanPath(QDir::fromNativeSeparators(m_path->text().trimmed()));
}

QHash<QString, double> SettingsDialog::weights() const {
    QHash<QString, double> w;
    for (auto it = m_weightBoxes.cbegin(); it != m_weightBoxes.cend(); ++it) w.insert(it.key(), it.value()->value());
    return w;
}

int SettingsDialog::resultCount() const { return m_resultCount->value(); }

void SettingsDialog::showDatabasePage() {
    m_tabs->setCurrentIndex(0);
    m_path->setFocus();
    m_path->selectAll();
}

void SettingsDialog::browseForDatabase() {
    QString start = databasePath();
    if (!QFileInfo(start).absoluteDir().exists()) start = AppSettings::defaultDatabasePath();
    const QString file = QFileDialog::getOpenFileName(this, tr("Select Paint Database"), start,
                                                      tr("CSV files (*.csv);;All files (*)"));
    if (!file.isEmpty()) m_path->setText(QDir::toNativeSeparators(file));
}

void SettingsDialog::restoreFactoryWeights() {
    for (const auto& def : paintdb::attributeDefs()) {
        if (QDoubleSpinBox* box = m_weightBoxes.value(toQ(def.key))) box->setValue(def.defaultWeight());
    }
}

void SettingsDialog::accept() {
    const QString path = databasePath();
    if (path.isEmpty()) {
        QMessageBox::warning(this, tr("Options"), tr("Please choose the database CSV file."));
        showDatabasePage();
        return;
    }
    if (!QFileInfo(path).isFile()) {
        const auto answer = QMessageBox::question(
            this, tr("Options"),
            tr("The file\n%1\ndoes not exist. Use this path anyway?").arg(QDir::toNativeSeparators(path)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            showDatabasePage();
            return;
        }
    }
    QDialog::accept();
}
