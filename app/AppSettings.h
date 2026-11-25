#pragma once

#include <QByteArray>
#include <QHash>
#include <QSettings>
#include <QString>

// Persistent settings stored in settings.ini next to the executable.
class AppSettings {
public:
    AppSettings();

    static QString iniPath();
    static QString defaultDatabasePath();  // <exe folder>/data/paints.csv

    // Absolute path to the CSV database. Paths inside the application folder are stored
    // relative to it so the install folder can be moved.
    QString databasePath() const;
    void setDatabasePath(const QString& path);

    static double factoryWeight(const QString& key);
    static QHash<QString, double> factoryWeights();
    QHash<QString, double> weights() const;  // stored values, factory defaults for missing/invalid
    void setWeights(const QHash<QString, double>& weights);

    int resultCount() const;
    void setResultCount(int count);

    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& geometry);
    QByteArray splitterState(const QString& name) const;
    void setSplitterState(const QString& name, const QByteArray& state);

    // Writes any missing keys with their defaults so settings.ini is complete and editable.
    void writeMissingDefaults();

    // Flushes to disk. Returns false (with a message) if the file could not be written,
    // e.g. when the application is installed in a read-only folder.
    bool save(QString* errorMessage = nullptr);

    static constexpr int kDefaultResultCount = 5;
    static constexpr int kMaxResultCount = 100;

private:
    QSettings m_settings;
};
