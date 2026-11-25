#include "AppSettings.h"

#include "AppInfo.h"
#include "paint_schema.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace {
const QString kDatabasePath = QStringLiteral("database/csvPath");
const QString kWeightsGroup = QStringLiteral("weights");
const QString kResultCount = QStringLiteral("search/resultCount");
const QString kGeometry = QStringLiteral("ui/geometry");
const QString kSplitterPrefix = QStringLiteral("ui/splitter_");
const QString kDefaultRelativeDb = QStringLiteral("data/paints.csv");

QString appDir() { return QCoreApplication::applicationDirPath(); }
}  // namespace

AppSettings::AppSettings() : m_settings(iniPath(), QSettings::IniFormat) {}

QString AppSettings::iniPath() { return QDir(appDir()).filePath(QStringLiteral("settings.ini")); }

QString AppSettings::defaultDatabasePath() { return QDir::cleanPath(QDir(appDir()).filePath(kDefaultRelativeDb)); }

QString AppSettings::databasePath() const {
    const QString stored = m_settings.value(kDatabasePath).toString().trimmed();
    if (stored.isEmpty()) return defaultDatabasePath();
    if (QFileInfo(stored).isRelative()) return QDir::cleanPath(QDir(appDir()).filePath(stored));
    return QDir::cleanPath(QDir::fromNativeSeparators(stored));
}

void AppSettings::setDatabasePath(const QString& path) {
    const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString relative = QDir(appDir()).relativeFilePath(absolute);
    // relativeFilePath() returns an absolute path when the drives differ.
    const bool insideAppDir = QFileInfo(relative).isRelative() && !relative.startsWith(QStringLiteral(".."));
    m_settings.setValue(kDatabasePath, insideAppDir ? relative : QDir::toNativeSeparators(absolute));
}

double AppSettings::factoryWeight(const QString& key) {
    if (auto idx = paintdb::attributeIndex(toStd(key))) return paintdb::attributeDefs()[*idx].defaultWeight();
    return 1.0;
}

QHash<QString, double> AppSettings::factoryWeights() {
    QHash<QString, double> w;
    for (const auto& def : paintdb::attributeDefs()) w.insert(toQ(def.key), def.defaultWeight());
    return w;
}

QHash<QString, double> AppSettings::weights() const {
    QHash<QString, double> w = factoryWeights();
    for (auto it = w.begin(); it != w.end(); ++it) {
        bool ok = false;
        const double v = m_settings.value(kWeightsGroup + QLatin1Char('/') + it.key()).toDouble(&ok);
        if (ok && v >= 0.0 && v <= 100.0) it.value() = v;
    }
    return w;
}

void AppSettings::setWeights(const QHash<QString, double>& weights) {
    for (auto it = weights.cbegin(); it != weights.cend(); ++it)
        m_settings.setValue(kWeightsGroup + QLatin1Char('/') + it.key(), it.value());
}

int AppSettings::resultCount() const {
    bool ok = false;
    const int v = m_settings.value(kResultCount, kDefaultResultCount).toInt(&ok);
    return ok ? std::clamp(v, 1, kMaxResultCount) : kDefaultResultCount;
}

void AppSettings::setResultCount(int count) { m_settings.setValue(kResultCount, std::clamp(count, 1, kMaxResultCount)); }

QByteArray AppSettings::windowGeometry() const { return m_settings.value(kGeometry).toByteArray(); }

void AppSettings::setWindowGeometry(const QByteArray& geometry) { m_settings.setValue(kGeometry, geometry); }

QByteArray AppSettings::splitterState(const QString& name) const {
    return m_settings.value(kSplitterPrefix + name).toByteArray();
}

void AppSettings::setSplitterState(const QString& name, const QByteArray& state) {
    m_settings.setValue(kSplitterPrefix + name, state);
}

void AppSettings::writeMissingDefaults() {
    if (!m_settings.contains(kDatabasePath)) m_settings.setValue(kDatabasePath, kDefaultRelativeDb);
    if (!m_settings.contains(kResultCount)) m_settings.setValue(kResultCount, kDefaultResultCount);
    for (const auto& def : paintdb::attributeDefs()) {
        const QString key = kWeightsGroup + QLatin1Char('/') + toQ(def.key);
        if (!m_settings.contains(key)) m_settings.setValue(key, def.defaultWeight());
    }
}

bool AppSettings::save(QString* errorMessage) {
    m_settings.sync();
    switch (m_settings.status()) {
        case QSettings::NoError:
            return true;
        case QSettings::AccessError:
            if (errorMessage)
                *errorMessage = QCoreApplication::translate(
                                    "AppSettings",
                                    "Settings could not be written to:\n%1\n\n"
                                    "The application folder may be read-only (for example under Program Files). "
                                    "Changes will be lost when the application closes.")
                                    .arg(QDir::toNativeSeparators(iniPath()));
            return false;
        case QSettings::FormatError:
            if (errorMessage)
                *errorMessage = QCoreApplication::translate("AppSettings",
                                                            "The settings file is damaged and could not be read:\n%1")
                                    .arg(QDir::toNativeSeparators(iniPath()));
            return false;
    }
    return true;
}
