#pragma once

#include "paint_match.hpp"
#include "paint_schema.hpp"

#include <QHash>
#include <QStringList>
#include <QWidget>

#include <vector>

class CheckableComboBox;
class OptionalSpinBox;
class NoWheelComboBox;
class NoWheelDoubleSpinBox;
class QCheckBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QToolButton;

// One input row per attribute: target, min/max (numeric only), weight and "required".
// Leaving the target at "Any" (and min/max empty) leaves the attribute out of the search.
class CriteriaPanel : public QWidget {
    Q_OBJECT
public:
    explicit CriteriaPanel(QWidget* parent = nullptr);

    // Refreshes the choice lists from the database (nullptr = workbook lists only) and
    // disables attributes that have no data in it. Current selections are kept where possible.
    void setDatabase(const paintdb::Database* db);

    void setDefaultWeights(const QHash<QString, double>& weights, bool applyToRows);
    void resetWeights();
    void clearCriteria();

    struct BuildResult {
        paintmatch::Query query;
        QStringList errors;
    };
    BuildResult buildQuery() const;

    // Short description of the criterion for attribute `attr`, e.g. "35 °F · max 45 °F · required".
    QString criterionSummary(std::size_t attr) const;
    double weightFor(std::size_t attr) const;

signals:
    void searchRequested();
    void criteriaChanged();

private:
    struct Row {
        const paintdb::AttributeDef* def = nullptr;
        QLabel* label = nullptr;
        NoWheelComboBox* combo = nullptr;     // Categorical / yes-no
        CheckableComboBox* multi = nullptr;   // Set
        QLineEdit* text = nullptr;            // Keywords
        OptionalSpinBox* target = nullptr;    // Numeric
        OptionalSpinBox* min = nullptr;
        OptionalSpinBox* max = nullptr;
        NoWheelDoubleSpinBox* weight = nullptr;
        QCheckBox* required = nullptr;
        QToolButton* clear = nullptr;
        bool available = true;
    };

    void buildUi();
    QWidget* createTargetWidget(Row& row);
    OptionalSpinBox* createNumberBox(const paintdb::AttributeDef& def, const QString& noneText);
    void setComboOptions(Row& row, const QStringList& options);
    bool hasTarget(const Row& row) const;
    bool isActive(const Row& row) const;
    void clearRow(Row& row);
    void updateRowState(Row& row);
    QString formatNumber(const paintdb::AttributeDef& def, double v) const;

    QGridLayout* m_grid = nullptr;
    std::vector<Row> m_rows;
    QHash<QString, double> m_defaultWeights;
};
