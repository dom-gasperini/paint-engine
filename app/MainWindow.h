#pragma once

#include "AppSettings.h"
#include "paint_match.hpp"
#include "paint_schema.hpp"

#include <QMainWindow>

#include <memory>

class CriteriaPanel;
class QAction;
class QLabel;
class QSpinBox;
class QSplitter;
class QTableView;
class QTextBrowser;
class ResultsModel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Called once the window is shown: writes default settings and loads the database,
    // asking for a new path if the file is missing or unreadable.
    void initialize();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void buildMenus();

    bool loadDatabase(const QString& path, QString* error);
    bool ensureDatabase(QString path);  // load, prompting until loaded or the user gives up
    void openDatabase();
    void reloadDatabase();
    void showLoadReport();
    void updateDatabaseStatus();

    void runSearch();
    void clearResults(const QString& message);
    void showDetails();
    QString detailsHtml(const paintmatch::Match& match, int rank) const;

    bool openOptions(bool reloadIfPathChanged);
    void showAbout();
    void saveSettings();

    AppSettings m_settings;
    std::unique_ptr<paintdb::Database> m_db;
    std::unique_ptr<paintmatch::Matcher> m_matcher;
    QString m_databasePath;  // path of the loaded database
    paintmatch::Query m_lastQuery;
    bool m_settingsWarningShown = false;

    CriteriaPanel* m_criteria = nullptr;
    ResultsModel* m_results = nullptr;
    QTableView* m_table = nullptr;
    QTextBrowser* m_details = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_dbStatus = nullptr;
    QSpinBox* m_resultCount = nullptr;
    QSplitter* m_mainSplitter = nullptr;
    QSplitter* m_resultSplitter = nullptr;
    QAction* m_searchAction = nullptr;
    QAction* m_reloadAction = nullptr;
    QAction* m_reportAction = nullptr;
};
