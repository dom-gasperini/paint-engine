#include "MainWindow.h"

#include "AppInfo.h"
#include "CriteriaPanel.h"
#include "ResultsModel.h"
#include "SettingsDialog.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStringDecoder>
#include <QSysInfo>
#include <QTableView>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <exception>

// ====================================================================== helpers

QString compilerDescription() {
#if defined(__clang__)
#if defined(_MSC_VER)
    return QStringLiteral("Clang-cl %1.%2.%3 (MSVC %4)")
        .arg(__clang_major__).arg(__clang_minor__).arg(__clang_patchlevel__).arg(_MSC_VER);
#else
    return QStringLiteral("Clang %1.%2.%3").arg(__clang_major__).arg(__clang_minor__).arg(__clang_patchlevel__);
#endif
#elif defined(_MSC_VER)
    return QStringLiteral("Microsoft Visual C++ %1.%2 (_MSC_FULL_VER %3)")
        .arg(_MSC_VER / 100).arg(_MSC_VER % 100).arg(_MSC_FULL_VER);
#elif defined(__GNUC__)
#if defined(__MINGW64__) || defined(__MINGW32__)
    return QStringLiteral("MinGW GCC %1.%2.%3").arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#else
    return QStringLiteral("GCC %1.%2.%3").arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#endif
#else
    return QStringLiteral("unknown compiler");
#endif
}

namespace {

// Excel's "CSV UTF-8" is UTF-8 with a BOM; plain "CSV (Comma delimited)" uses the Windows
// code page. Decode as UTF-8 and fall back to the system code page if that fails.
std::string decodeCsv(const QByteArray& bytes) {
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8(bytes);
    if (utf8.hasError()) {
        QStringDecoder system(QStringDecoder::System);
        text = system(bytes);
    }
    return text.toStdString();
}

QString native(const QString& path) { return QDir::toNativeSeparators(path); }

bool samePath(const QString& a, const QString& b) {
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return QDir::cleanPath(QFileInfo(a).absoluteFilePath()).compare(QDir::cleanPath(QFileInfo(b).absoluteFilePath()),
                                                                    cs) == 0;
}

QString htmlRow(const QString& label, const QString& valueHtml) {
    return QStringLiteral("<tr><td style='padding:2px 10px 2px 0; color:#707070;' valign='top'>%1</td>"
                          "<td style='padding:2px 0;'>%2</td></tr>")
        .arg(label.toHtmlEscaped(), valueHtml);
}

QString linkOrText(const QString& value) {
    if (value.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) ||
        value.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
        return QStringLiteral("<a href='%1'>%2</a>").arg(value.toHtmlEscaped(), QObject::tr("Open"));
    return value.toHtmlEscaped();
}

}  // namespace

// ====================================================================== construction

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    buildUi();
    buildMenus();

    const QByteArray geometry = m_settings.windowGeometry();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    else resize(1400, 860);

    const QByteArray mainState = m_settings.splitterState(QStringLiteral("main"));
    if (!mainState.isEmpty()) m_mainSplitter->restoreState(mainState);
    const QByteArray resultState = m_settings.splitterState(QStringLiteral("results"));
    if (!resultState.isEmpty()) m_resultSplitter->restoreState(resultState);

    setWindowTitle(QStringLiteral("%1 v%2").arg(QStringLiteral(APP_NAME), QStringLiteral(APP_VERSION)));
    clearResults(tr("Set one or more criteria on the left and press Search."));
    updateDatabaseStatus();
}

MainWindow::~MainWindow() {
    // The model points into m_db; detach it before the database is destroyed.
    if (m_results) m_results->clear();
}

void MainWindow::buildUi() {
    // ---------------------------------------------------------------- left: criteria
    m_criteria = new CriteriaPanel;
    m_criteria->setDefaultWeights(m_settings.weights(), true);

    auto* scroll = new QScrollArea;
    scroll->setWidget(m_criteria);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(m_criteria->sizeHint().width() + 24);

    auto* clearButton = new QPushButton(tr("Clear Criteria"));
    auto* resetWeightsButton = new QPushButton(tr("Reset Weights"));
    resetWeightsButton->setToolTip(tr("Set every weight back to the defaults from Options"));
    m_resultCount = new QSpinBox;
    m_resultCount->setRange(1, AppSettings::kMaxResultCount);
    m_resultCount->setValue(m_settings.resultCount());
    m_resultCount->setToolTip(tr("Number of ranked results to show"));
    auto* searchButton = new QPushButton(tr("Search"));
    searchButton->setDefault(true);
    searchButton->setToolTip(tr("Find the closest matches (Ctrl+Enter)"));
    QFont bold = searchButton->font();
    bold.setBold(true);
    searchButton->setFont(bold);
    searchButton->setMinimumWidth(110);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(clearButton);
    buttonRow->addWidget(resetWeightsButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(new QLabel(tr("Results:")));
    buttonRow->addWidget(m_resultCount);
    buttonRow->addWidget(searchButton);

    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 4, 0);
    leftLayout->addWidget(scroll, 1);
    leftLayout->addLayout(buttonRow);

    // ---------------------------------------------------------------- right: results + details
    m_summary = new QLabel;
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);

    m_results = new ResultsModel(this);
    m_table = new QTableView;
    m_table->setModel(m_results);
    m_table->setItemDelegateForColumn(ResultsModel::MatchColumn, new MatchBarDelegate(m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    auto* resultsPane = new QWidget;
    auto* resultsLayout = new QVBoxLayout(resultsPane);
    resultsLayout->setContentsMargins(4, 0, 0, 0);
    resultsLayout->addWidget(m_summary);
    resultsLayout->addWidget(m_table, 1);

    m_details = new QTextBrowser;
    m_details->setOpenExternalLinks(true);
    m_details->setPlaceholderText(tr("Select a result to see the full product details."));

    m_resultSplitter = new QSplitter(Qt::Vertical);
    m_resultSplitter->addWidget(resultsPane);
    m_resultSplitter->addWidget(m_details);
    m_resultSplitter->setStretchFactor(0, 3);
    m_resultSplitter->setStretchFactor(1, 2);

    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainSplitter->addWidget(left);
    m_mainSplitter->addWidget(m_resultSplitter);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setChildrenCollapsible(false);
    setCentralWidget(m_mainSplitter);

    m_dbStatus = new QLabel;
    statusBar()->addPermanentWidget(m_dbStatus);

    // ---------------------------------------------------------------- actions
    m_searchAction = new QAction(tr("&Search"), this);
    m_searchAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Return), QKeySequence(Qt::CTRL | Qt::Key_Enter)});
    m_searchAction->setShortcutContext(Qt::WindowShortcut);
    addAction(m_searchAction);

    connect(m_searchAction, &QAction::triggered, this, &MainWindow::runSearch);
    connect(searchButton, &QPushButton::clicked, this, &MainWindow::runSearch);
    connect(m_criteria, &CriteriaPanel::searchRequested, this, &MainWindow::runSearch);
    connect(clearButton, &QPushButton::clicked, m_criteria, &CriteriaPanel::clearCriteria);
    connect(resetWeightsButton, &QPushButton::clicked, m_criteria, &CriteriaPanel::resetWeights);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &MainWindow::showDetails);
}

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* open = file->addAction(tr("&Open Database…"), this, &MainWindow::openDatabase);
    open->setShortcut(QKeySequence::Open);
    m_reloadAction = file->addAction(tr("&Reload Database"), this, &MainWindow::reloadDatabase);
    m_reloadAction->setShortcut(QKeySequence::Refresh);
    m_reportAction = file->addAction(tr("Database Load &Report…"), this, &MainWindow::showLoadReport);
    file->addSeparator();
    QAction* quit = file->addAction(tr("E&xit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    QMenu* search = menuBar()->addMenu(tr("&Search"));
    search->addAction(m_searchAction);
    search->addAction(tr("&Clear Criteria"), m_criteria, &CriteriaPanel::clearCriteria);
    search->addAction(tr("&Reset Weights"), m_criteria, &CriteriaPanel::resetWeights);

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    QAction* options = tools->addAction(tr("&Options…"), this, [this] { openOptions(true); });
    options->setShortcut(QKeySequence::Preferences);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About %1").arg(QStringLiteral(APP_NAME)), this, &MainWindow::showAbout);
    help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

// ====================================================================== startup / settings

void MainWindow::initialize() {
    m_settings.writeMissingDefaults();
    saveSettings();
    ensureDatabase(m_settings.databasePath());
}

void MainWindow::saveSettings() {
    QString error;
    if (m_settings.save(&error)) return;
    statusBar()->showMessage(tr("Settings could not be saved."), 8000);
    if (!m_settingsWarningShown) {
        m_settingsWarningShown = true;
        QMessageBox::warning(this, tr("Settings Not Saved"), error);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    m_settings.setWindowGeometry(saveGeometry());
    m_settings.setSplitterState(QStringLiteral("main"), m_mainSplitter->saveState());
    m_settings.setSplitterState(QStringLiteral("results"), m_resultSplitter->saveState());
    m_settings.setResultCount(m_resultCount->value());
    QString ignored;
    m_settings.save(&ignored);  // already warned about write problems during the session
    event->accept();
}

// ====================================================================== database

bool MainWindow::loadDatabase(const QString& path, QString* error) {
    const QFileInfo info(path);
    if (path.trimmed().isEmpty()) {
        *error = tr("No database file is configured.");
        return false;
    }
    if (!info.exists()) {
        *error = tr("The database file was not found:\n%1").arg(native(path));
        return false;
    }
    if (!info.isFile()) {
        *error = tr("The database path is not a file:\n%1").arg(native(path));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = tr("The database file could not be opened:\n%1\n\n%2").arg(native(path), file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    std::unique_ptr<paintdb::Database> db;
    std::unique_ptr<paintmatch::Matcher> matcher;
    QString failure;
    try {
        db = std::make_unique<paintdb::Database>(paintdb::parseDatabase(decodeCsv(bytes), toStd(path)));
        matcher = std::make_unique<paintmatch::Matcher>(paintdb::engineSpecs());
        matcher->setCatalog(db->values);
    } catch (const std::exception& e) {
        failure = toQ(e.what());
    }
    QApplication::restoreOverrideCursor();

    if (!failure.isEmpty()) {
        *error = tr("The database file could not be read:\n%1\n\n%2").arg(native(path), failure);
        return false;
    }

    clearResults(tr("Database loaded. Set one or more criteria on the left and press Search."));
    m_matcher = std::move(matcher);
    m_db = std::move(db);
    m_databasePath = QDir::cleanPath(info.absoluteFilePath());
    m_criteria->setDatabase(m_db.get());
    updateDatabaseStatus();

    if (m_db->warningCount > 0)
        statusBar()->showMessage(
            tr("Loaded with %n warning(s); see File › Database Load Report.", nullptr,
               static_cast<int>(m_db->warningCount)),
            10000);
    return true;
}

bool MainWindow::ensureDatabase(QString path) {
    for (;;) {
        QString error;
        if (loadDatabase(path, &error)) return true;

        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Paint Database"));
        box.setText(tr("The paint database could not be loaded."));
        box.setInformativeText(error + QStringLiteral("\n\n") +
                               tr("Choose the CSV file to use. The new location is saved in the settings."));
        QPushButton* browse = box.addButton(tr("Browse for CSV…"), QMessageBox::AcceptRole);
        QPushButton* options = box.addButton(tr("Options…"), QMessageBox::ActionRole);
        QPushButton* skip = box.addButton(tr("Continue Without Database"), QMessageBox::RejectRole);
        box.setDefaultButton(browse);
        box.setEscapeButton(skip);
        box.exec();

        if (box.clickedButton() == browse) {
            QString start = QFileInfo(path).absolutePath();
            if (!QDir(start).exists()) start = QFileInfo(AppSettings::defaultDatabasePath()).absolutePath();
            const QString file = QFileDialog::getOpenFileName(this, tr("Select Paint Database"), start,
                                                              tr("CSV files (*.csv);;All files (*)"));
            if (file.isEmpty()) continue;  // back to the prompt
            path = file;
            m_settings.setDatabasePath(file);
            saveSettings();
        } else if (box.clickedButton() == options) {
            openOptions(false);
            path = m_settings.databasePath();
        } else {
            if (!m_db) {
                m_criteria->setDatabase(nullptr);
                clearResults(tr("No database is loaded. Use File › Open Database… to choose the CSV file."));
            }
            updateDatabaseStatus();
            return false;
        }
    }
}

void MainWindow::openDatabase() {
    QString start = m_databasePath.isEmpty() ? m_settings.databasePath() : m_databasePath;
    start = QFileInfo(start).absolutePath();
    const QString file = QFileDialog::getOpenFileName(this, tr("Open Paint Database"), start,
                                                      tr("CSV files (*.csv);;All files (*)"));
    if (file.isEmpty()) return;

    QString error;
    if (!loadDatabase(file, &error)) {
        QMessageBox::warning(this, tr("Paint Database"), error);
        return;
    }
    m_settings.setDatabasePath(file);
    saveSettings();
}

void MainWindow::reloadDatabase() {
    const QString path = m_databasePath.isEmpty() ? m_settings.databasePath() : m_databasePath;
    QString error;
    if (loadDatabase(path, &error)) {
        statusBar()->showMessage(tr("Database reloaded."), 4000);
        return;
    }
    if (m_db) QMessageBox::warning(this, tr("Paint Database"), error + tr("\n\nThe previous data is still in use."));
    else ensureDatabase(path);
}

void MainWindow::updateDatabaseStatus() {
    const bool loaded = m_db != nullptr;
    m_searchAction->setEnabled(loaded);
    m_reloadAction->setEnabled(true);
    m_reportAction->setEnabled(loaded);

    if (!loaded) {
        m_dbStatus->setText(tr("No database loaded"));
        m_dbStatus->setToolTip(QString());
        setWindowTitle(QStringLiteral("%1 v%2").arg(QStringLiteral(APP_NAME), QStringLiteral(APP_VERSION)));
        return;
    }
    QString text = tr("%1 — %n product(s)", nullptr, static_cast<int>(m_db->products.size()))
                       .arg(QFileInfo(m_databasePath).fileName());
    if (m_db->warningCount > 0) text += tr(" (%n warning(s))", nullptr, static_cast<int>(m_db->warningCount));
    m_dbStatus->setText(text);
    m_dbStatus->setToolTip(native(m_databasePath));
    setWindowTitle(QStringLiteral("%1 v%2 — %3")
                       .arg(QStringLiteral(APP_NAME), QStringLiteral(APP_VERSION), QFileInfo(m_databasePath).fileName()));
}

void MainWindow::showLoadReport() {
    if (!m_db) return;
    QStringList lines;
    lines << tr("File: %1").arg(native(m_databasePath));
    lines << tr("Products loaded: %1").arg(m_db->products.size());
    lines << tr("Blank rows skipped: %1").arg(m_db->blankRowsSkipped);
    if (!m_db->missingColumns.empty()) {
        QStringList cols;
        for (const auto& c : m_db->missingColumns) cols << toQ(c);
        lines << tr("Columns not found: %1").arg(cols.join(QStringLiteral(", ")));
    }
    if (!m_db->ignoredColumns.empty()) {
        QStringList cols;
        for (const auto& c : m_db->ignoredColumns) cols << toQ(c);
        lines << tr("Columns ignored: %1").arg(cols.join(QStringLiteral(", ")));
    }
    lines << tr("Warnings: %1").arg(m_db->warningCount);

    QMessageBox box(this);
    box.setWindowTitle(tr("Database Load Report"));
    box.setIcon(m_db->warningCount > 0 ? QMessageBox::Warning : QMessageBox::Information);
    box.setText(lines.join(QLatin1Char('\n')));
    if (!m_db->warnings.empty()) {
        QStringList details;
        for (const auto& w : m_db->warnings) details << toQ(w);
        if (m_db->warningCount > m_db->warnings.size())
            details << tr("… and %1 more").arg(m_db->warningCount - m_db->warnings.size());
        box.setDetailedText(details.join(QLatin1Char('\n')));
    }
    box.exec();
}

// ====================================================================== search

void MainWindow::clearResults(const QString& message) {
    m_results->clear();
    m_details->clear();
    m_summary->setText(message.toHtmlEscaped());
}

void MainWindow::runSearch() {
    if (!m_matcher || !m_db) {
        statusBar()->showMessage(tr("No database is loaded."), 5000);
        return;
    }

    CriteriaPanel::BuildResult built = m_criteria->buildQuery();
    if (!built.errors.isEmpty()) {
        QMessageBox::warning(this, tr("Check the Criteria"), built.errors.join(QLatin1Char('\n')));
        return;
    }
    built.query.limit = static_cast<std::size_t>(m_resultCount->value());

    paintmatch::SearchResult result;
    try {
        result = m_matcher->search(built.query);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Search"), toQ(e.what()));
        return;
    }
    m_lastQuery = built.query;

    QStringList headers;
    for (std::size_t attr : result.activeAttributes) headers << m_criteria->criterionSummary(attr);
    m_details->clear();
    m_results->setResults(m_db.get(), result, headers);

    // Size columns to their content, but keep long lists (substrates, sheens) from taking over.
    m_table->resizeColumnsToContents();
    m_table->resizeRowsToContents();
    QHeaderView* header = m_table->horizontalHeader();
    for (int c = ResultsModel::FirstAttributeColumn; c < m_results->columnCount(); ++c)
        if (header->sectionSize(c) > 260) header->resizeSection(c, 260);
    if (header->sectionSize(ResultsModel::ProductColumn) > 320) header->resizeSection(ResultsModel::ProductColumn, 320);

    QString summary;
    if (result.activeAttributes.empty()) {
        summary = tr("No criteria set: showing the first %n product(s) of the database.", nullptr,
                     static_cast<int>(result.matches.size()));
    } else if (result.candidates == 0) {
        summary = tr("<b>No products</b> meet the Required / Min / Max filters (%1 products in the database). "
                     "Relax a filter and search again.")
                      .arg(result.catalogSize);
    } else {
        summary = tr("Top <b>%1</b> of %2 matching product(s) (%3 in the database). "
                     "Cells show the product's value and how well it matches; green is closer.")
                      .arg(result.matches.size())
                      .arg(result.candidates)
                      .arg(result.catalogSize);
    }
    m_summary->setText(summary);

    if (m_results->rowCount() > 0) {
        m_table->setCurrentIndex(m_results->index(0, ResultsModel::ProductColumn));
        showDetails();
    }
}

void MainWindow::showDetails() {
    const QModelIndex current = m_table->currentIndex();
    const paintmatch::Match* match = current.isValid() ? m_results->matchAt(current.row()) : nullptr;
    if (!match || !m_db) {
        m_details->clear();
        return;
    }
    m_details->setHtml(detailsHtml(*match, current.row() + 1));
}

QString MainWindow::detailsHtml(const paintmatch::Match& match, int rank) const {
    const paintdb::Product& p = m_db->products[match.row];
    const auto& defs = paintdb::attributeDefs();
    QString html;

    html += QStringLiteral("<h2 style='margin-bottom:2px'>%1</h2>").arg(toQ(p.displayName()).toHtmlEscaped());
    const QString purpose = toQ(p.field(paintdb::col::AdvertisedPurpose));
    if (!purpose.isEmpty()) html += QStringLiteral("<p style='margin-top:0'><i>%1</i></p>").arg(purpose.toHtmlEscaped());
    html += tr("<p><b>Rank %1 — %2% match</b></p>").arg(rank).arg(match.score * 100.0, 0, 'f', 1);

    // Score breakdown
    if (!match.breakdown.empty()) {
        html += tr("<h3>Score breakdown</h3>");
        html += QStringLiteral("<table cellspacing='0' cellpadding='3' border='0'>");
        html += tr("<tr><th align='left'>Attribute</th><th align='left'>Target</th><th align='left'>Product</th>"
                   "<th align='right'>Score</th><th align='right'>Weight</th></tr>");
        for (const auto& as : match.breakdown) {
            const auto& def = defs[as.attribute];
            const QString value = toQ(paintdb::formatValue(def, m_db->values[match.row][as.attribute]));
            const QString target = m_criteria->criterionSummary(as.attribute);
            double weight = def.defaultWeight();
            auto it = m_lastQuery.criteria.find(def.key);
            if (it != m_lastQuery.criteria.end() && it->second.weight) weight = *it->second.weight;

            QString score = tr("filter");
            QString bg;
            if (as.scored) {
                score = QStringLiteral("%1%").arg(as.similarity * 100.0, 0, 'f', 0);
                bg = QStringLiteral(" style='background-color:%1; color:black'")
                         .arg(ResultsModel::scoreColor(as.similarity).name());
            }
            html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td align='right'%4>%5</td>"
                                   "<td align='right'>%6</td></tr>")
                        .arg(toQ(def.label).toHtmlEscaped(), target.toHtmlEscaped(),
                             value.isEmpty() ? QStringLiteral("—") : value.toHtmlEscaped(), bg, score,
                             QString::number(weight));
        }
        html += QStringLiteral("</table>");
    }

    // Every column of the product
    html += tr("<h3>Product details</h3>");
    html += QStringLiteral("<table cellspacing='0' cellpadding='0' border='0'>");
    const auto& columns = paintdb::knownColumns();
    for (std::size_t c = 0; c < columns.size(); ++c) {
        const QString name = toQ(columns[c]);
        if (columns[c] == paintdb::col::AdvertisedPurpose) continue;
        const QString value = c < p.fields.size() ? toQ(p.fields[c]) : QString();
        if (value.isEmpty() || value.compare(QLatin1String("NA"), Qt::CaseInsensitive) == 0) continue;
        html += htmlRow(name, linkOrText(value));
    }
    html += QStringLiteral("</table>");
    html += tr("<p style='color:gray'>CSV line %1</p>").arg(p.sourceLine);
    return html;
}

// ====================================================================== dialogs

bool MainWindow::openOptions(bool reloadIfPathChanged) {
    const QString oldPath = m_settings.databasePath();
    SettingsDialog dialog(oldPath, m_settings.weights(), m_resultCount->value(), this);
    if (!m_db) dialog.showDatabasePage();
    if (dialog.exec() != QDialog::Accepted) return false;

    const QString newPath = dialog.databasePath();
    const QHash<QString, double> oldWeights = m_settings.weights();
    const QHash<QString, double> newWeights = dialog.weights();

    m_settings.setDatabasePath(newPath);
    m_settings.setWeights(newWeights);
    m_settings.setResultCount(dialog.resultCount());
    saveSettings();

    m_resultCount->setValue(dialog.resultCount());
    if (newWeights != oldWeights) {
        m_criteria->setDefaultWeights(newWeights, true);
        statusBar()->showMessage(tr("Default weights updated and applied to the current search."), 5000);
    }

    if (reloadIfPathChanged && (!m_db || !samePath(newPath, m_databasePath))) ensureDatabase(newPath);
    return true;
}

void MainWindow::showAbout() {
    const QString text =
        tr("<h3>%1</h3>"
           "<p><b>Version v%2</b></p>"
           "<p>Finds the paint products in the database that best match the attributes you choose, "
           "ranked by a weighted similarity score.</p>"
           "<table cellspacing='0' cellpadding='2'>"
           "<tr><td>Built with Qt:</td><td>%3</td></tr>"
           "<tr><td>Running on Qt:</td><td>%4</td></tr>"
           "<tr><td>Compiler:</td><td>%5</td></tr>"
           "<tr><td>Architecture:</td><td>%6</td></tr>"
           "<tr><td>Build date:</td><td>%7</td></tr>"
           "</table>")
            .arg(QStringLiteral(APP_NAME), QStringLiteral(APP_VERSION), QStringLiteral(QT_VERSION_STR),
                 QString::fromLatin1(qVersion()), compilerDescription().toHtmlEscaped(),
                 QSysInfo::buildCpuArchitecture().toHtmlEscaped(), QStringLiteral(__DATE__));
    QMessageBox::about(this, tr("About %1").arg(QStringLiteral(APP_NAME)), text);
}
