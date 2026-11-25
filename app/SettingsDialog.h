#pragma once

#include <QDialog>
#include <QHash>
#include <QString>

class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;
class QTabWidget;

// Options: database location, default result count and the default attribute weights.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(const QString& databasePath, const QHash<QString, double>& weights, int resultCount,
                   QWidget* parent = nullptr);

    QString databasePath() const;
    QHash<QString, double> weights() const;
    int resultCount() const;

    void showDatabasePage();

public slots:
    void accept() override;

private:
    void browseForDatabase();
    void restoreFactoryWeights();

    QTabWidget* m_tabs = nullptr;
    QLineEdit* m_path = nullptr;
    QSpinBox* m_resultCount = nullptr;
    QHash<QString, QDoubleSpinBox*> m_weightBoxes;
};
