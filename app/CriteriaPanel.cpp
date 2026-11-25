#include "CriteriaPanel.h"

#include "AppInfo.h"
#include "AppSettings.h"
#include "InputWidgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QStyle>
#include <QSignalBlocker>
#include <QToolButton>

#include <initializer_list>

namespace {

enum GridColumn { ColLabel, ColTarget, ColMin, ColMax, ColWeight, ColRequired, ColClear, ColumnCount };

QString directionArrow(const paintdb::AttributeDef& def) {
    if (!def.isNumeric()) return {};
    switch (def.spec.direction) {
        case paintmatch::Direction::HigherIsBetter: return QStringLiteral(" ↑");  // ↑
        case paintmatch::Direction::LowerIsBetter:  return QStringLiteral(" ↓");  // ↓
        case paintmatch::Direction::Symmetric:      return {};
    }
    return {};
}

QString directionText(const paintdb::AttributeDef& def) {
    switch (def.spec.direction) {
        case paintmatch::Direction::HigherIsBetter:
            return CriteriaPanel::tr("Higher is better: values above the target are barely penalised, "
                                     "values below it are penalised harder.");
        case paintmatch::Direction::LowerIsBetter:
            return CriteriaPanel::tr("Lower is better: values below the target are barely penalised, "
                                     "values above it are penalised harder.");
        case paintmatch::Direction::Symmetric:
            return CriteriaPanel::tr("Closeness counts equally in both directions.");
    }
    return {};
}

QString labelToolTip(const paintdb::AttributeDef& def) {
    const QString hint = toQ(def.hint);
    if (!def.isNumeric()) return hint;
    return hint.isEmpty() ? directionText(def) : hint + QLatin1Char('\n') + directionText(def);
}

QStringList toStringList(const std::vector<std::string>& v) {
    QStringList out;
    out.reserve(static_cast<int>(v.size()));
    for (const auto& s : v) out << toQ(s);
    return out;
}

}  // namespace

CriteriaPanel::CriteriaPanel(QWidget* parent) : QWidget(parent) {
    m_defaultWeights = AppSettings::factoryWeights();
    buildUi();
    setDatabase(nullptr);
}

// ------------------------------------------------------------------ construction

OptionalSpinBox* CriteriaPanel::createNumberBox(const paintdb::AttributeDef& def, const QString& noneText) {
    auto* box = new OptionalSpinBox(def.inputMin, def.inputMax, def.inputStep, def.decimals, noneText, this);
    const QString unit = toQ(def.unit);
    if (unit == QLatin1String("$")) box->setPrefix(QStringLiteral("$"));
    else if (unit == QLatin1String("%")) box->setSuffix(QStringLiteral("%"));
    else if (!unit.isEmpty()) box->setSuffix(QLatin1Char(' ') + unit);
    return box;
}

QWidget* CriteriaPanel::createTargetWidget(Row& row) {
    const paintdb::AttributeDef& def = *row.def;
    const QString hint = toQ(def.hint);

    switch (def.spec.kind) {
        case paintmatch::Kind::Numeric: {
            row.target = createNumberBox(def, tr("Any"));
            row.target->setToolTip(tr("Target value. %1\n%2").arg(directionText(def), hint));
            connect(row.target, &OptionalSpinBox::optionalValueChanged, this, &CriteriaPanel::criteriaChanged);
            return row.target;
        }
        case paintmatch::Kind::Categorical: {
            row.combo = new NoWheelComboBox(this);
            row.combo->setToolTip(hint);
            connect(row.combo, &QComboBox::currentIndexChanged, this, &CriteriaPanel::criteriaChanged);
            return row.combo;
        }
        case paintmatch::Kind::Set: {
            row.multi = new CheckableComboBox(tr("Any"), this);
            connect(row.multi, &CheckableComboBox::checkedItemsChanged, this, &CriteriaPanel::criteriaChanged);
            return row.multi;
        }
        case paintmatch::Kind::Keywords: {
            row.text = new QLineEdit(this);
            row.text->setPlaceholderText(tr("Any  (e.g. stain blocking, mildew)"));
            row.text->setClearButtonEnabled(true);
            row.text->setToolTip(hint);
            connect(row.text, &QLineEdit::textChanged, this, &CriteriaPanel::criteriaChanged);
            connect(row.text, &QLineEdit::returnPressed, this, &CriteriaPanel::searchRequested);
            return row.text;
        }
    }
    return nullptr;
}

void CriteriaPanel::buildUi() {
    m_grid = new QGridLayout(this);
    m_grid->setHorizontalSpacing(8);
    m_grid->setVerticalSpacing(4);
    m_grid->setColumnStretch(ColTarget, 1);

    int gridRow = 0;
    const QStringList headers = {tr("Attribute"), tr("Target"), tr("Min"), tr("Max"), tr("Weight"), tr("Req."), {}};
    const QStringList headerTips = {
        {},
        tr("What you are looking for. Leave at \"Any\" to ignore the attribute."),
        tr("Hard floor: products whose value is below this are excluded."),
        tr("Hard ceiling: products whose value is above this are excluded."),
        tr("Importance of the attribute in the match score (0 = not scored)."),
        tr("Required: exclude products that do not fully meet the target."),
        {}};
    for (int c = 0; c < ColumnCount; ++c) {
        auto* h = new QLabel(headers[c], this);
        QFont f = h->font();
        f.setBold(true);
        h->setFont(f);
        h->setToolTip(headerTips[c]);
        m_grid->addWidget(h, gridRow, c, c == ColRequired ? Qt::AlignHCenter : Qt::Alignment());
    }
    ++gridRow;

    const auto& defs = paintdb::attributeDefs();
    m_rows.reserve(defs.size());  // rows are referenced by index from the lambdas below
    std::string currentGroup;

    for (std::size_t i = 0; i < defs.size(); ++i) {
        const paintdb::AttributeDef& def = defs[i];

        if (def.group != currentGroup) {
            currentGroup = def.group;
            auto* line = new QFrame(this);
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Sunken);
            m_grid->addWidget(line, gridRow++, 0, 1, ColumnCount);
            auto* section = new QLabel(toQ(def.group), this);
            QFont f = section->font();
            f.setBold(true);
            if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * 1.05);
            section->setFont(f);
            m_grid->addWidget(section, gridRow++, 0, 1, ColumnCount);
        }

        m_rows.emplace_back();
        Row& row = m_rows.back();
        row.def = &def;

        QString labelText = toQ(def.label);
        if (!def.unit.empty() && def.unit != "$" && def.unit != "%") labelText += QStringLiteral(" (%1)").arg(toQ(def.unit));
        row.label = new QLabel(labelText + directionArrow(def), this);
        row.label->setToolTip(labelToolTip(def));

        QWidget* target = createTargetWidget(row);

        if (def.isNumeric()) {
            row.min = createNumberBox(def, QStringLiteral("—"));
            row.max = createNumberBox(def, QStringLiteral("—"));
            row.min->setToolTip(tr("Minimum (hard floor). Products below it are excluded.\n"
                                   "Leave at — for no minimum."));
            row.max->setToolTip(tr("Maximum (hard ceiling). Products above it are excluded.\n"
                                   "Leave at — for no maximum."));
            connect(row.min, &OptionalSpinBox::optionalValueChanged, this, &CriteriaPanel::criteriaChanged);
            connect(row.max, &OptionalSpinBox::optionalValueChanged, this, &CriteriaPanel::criteriaChanged);
        }

        row.weight = new NoWheelDoubleSpinBox(this);
        row.weight->setDecimals(1);
        row.weight->setRange(0.0, 10.0);
        row.weight->setSingleStep(0.5);
        connect(row.weight, &QDoubleSpinBox::valueChanged, this, &CriteriaPanel::criteriaChanged);

        row.required = new QCheckBox(this);
        row.required->setToolTip(def.isNumeric()
                                     ? tr("Required: exclude products that do not meet the target "
                                          "(in the direction shown by the arrow).")
                                     : tr("Required: exclude products that do not fully match the target."));
        connect(row.required, &QCheckBox::toggled, this, &CriteriaPanel::criteriaChanged);

        row.clear = new QToolButton(this);
        row.clear->setIcon(style()->standardIcon(QStyle::SP_LineEditClearButton));
        row.clear->setAutoRaise(true);
        row.clear->setToolTip(tr("Clear this attribute"));
        connect(row.clear, &QToolButton::clicked, this, [this, i] {
            clearRow(m_rows[i]);
            emit criteriaChanged();
        });

        m_grid->addWidget(row.label, gridRow, ColLabel);
        m_grid->addWidget(target, gridRow, ColTarget);
        if (row.min) m_grid->addWidget(row.min, gridRow, ColMin);
        if (row.max) m_grid->addWidget(row.max, gridRow, ColMax);
        m_grid->addWidget(row.weight, gridRow, ColWeight);
        m_grid->addWidget(row.required, gridRow, ColRequired, Qt::AlignHCenter);
        m_grid->addWidget(row.clear, gridRow, ColClear);
        ++gridRow;
    }
    m_grid->setRowStretch(gridRow, 1);

    // Keep row state (bold label, enabled "Req.") in sync with the inputs.
    connect(this, &CriteriaPanel::criteriaChanged, this, [this] {
        for (Row& r : m_rows) updateRowState(r);
    });

    resetWeights();
    for (Row& r : m_rows) updateRowState(r);
}

// ------------------------------------------------------------------ data

void CriteriaPanel::setComboOptions(Row& row, const QStringList& options) {
    const QString current = row.combo->currentData().toString();
    const QSignalBlocker block(row.combo);
    row.combo->clear();
    row.combo->addItem(tr("Any"), QString());
    for (const QString& o : options) row.combo->addItem(o, o);
    int idx = current.isEmpty() ? 0 : row.combo->findData(current, Qt::UserRole, Qt::MatchFixedString);
    row.combo->setCurrentIndex(idx < 0 ? 0 : idx);
}

void CriteriaPanel::setDatabase(const paintdb::Database* db) {
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        Row& row = m_rows[i];
        const paintdb::AttributeDef& def = *row.def;

        QStringList options;
        if (db && i < db->options.size() && !db->options[i].empty()) options = toStringList(db->options[i]);
        else options = toStringList(def.options);

        if (row.combo) setComboOptions(row, options);
        if (row.multi) row.multi->setItems(options);

        row.available = !db || (i < db->attributeHasData.size() && db->attributeHasData[i]);
        const QString unavailable = tr("Not available: the loaded database has no data for this attribute "
                                       "(column missing or empty).");
        for (QWidget* w : std::initializer_list<QWidget*>{row.combo, row.multi, row.text, row.target, row.min, row.max,
                                                          row.weight, row.required, row.clear}) {
            if (w) w->setEnabled(row.available);
        }
        row.label->setEnabled(row.available);
        row.label->setToolTip(row.available ? labelToolTip(def) : unavailable);
        updateRowState(row);
    }
    emit criteriaChanged();
}

void CriteriaPanel::setDefaultWeights(const QHash<QString, double>& weights, bool applyToRows) {
    m_defaultWeights = weights;
    if (applyToRows) resetWeights();
}

void CriteriaPanel::resetWeights() {
    for (Row& row : m_rows) {
        const QString key = toQ(row.def->key);
        const double w = m_defaultWeights.value(key, row.def->defaultWeight());
        row.weight->setValue(w);
        row.weight->setToolTip(tr("Importance in the match score (default %1). 0 = not scored; "
                                  "min/max and Required still apply.")
                                   .arg(w));
    }
}

void CriteriaPanel::clearRow(Row& row) {
    if (row.combo) row.combo->setCurrentIndex(0);
    if (row.multi) row.multi->clearChecks();
    if (row.text) row.text->clear();
    if (row.target) row.target->reset();
    if (row.min) row.min->reset();
    if (row.max) row.max->reset();
    row.required->setChecked(false);
    updateRowState(row);
}

void CriteriaPanel::clearCriteria() {
    for (Row& row : m_rows) clearRow(row);
    emit criteriaChanged();
}

// ------------------------------------------------------------------ state

bool CriteriaPanel::hasTarget(const Row& row) const {
    if (row.combo) return row.combo->currentIndex() > 0;
    if (row.multi) return !row.multi->checkedItems().isEmpty();
    if (row.text) return !row.text->text().trimmed().isEmpty();
    if (row.target) return row.target->hasValue();
    return false;
}

bool CriteriaPanel::isActive(const Row& row) const {
    return row.available &&
           (hasTarget(row) || (row.min && row.min->hasValue()) || (row.max && row.max->hasValue()));
}

void CriteriaPanel::updateRowState(Row& row) {
    QFont f = row.label->font();
    const bool active = isActive(row);
    if (f.bold() != active) {
        f.setBold(active);
        row.label->setFont(f);
    }
    const bool canRequire = row.available && hasTarget(row);
    row.required->setEnabled(canRequire);
    if (!canRequire && row.required->isChecked()) {
        const QSignalBlocker block(row.required);
        row.required->setChecked(false);
    }
}

double CriteriaPanel::weightFor(std::size_t attr) const {
    return attr < m_rows.size() ? m_rows[attr].weight->value() : 0.0;
}

QString CriteriaPanel::formatNumber(const paintdb::AttributeDef& def, double v) const {
    return toQ(paintdb::formatValue(def, paintmatch::number(v)));
}

// ------------------------------------------------------------------ query

CriteriaPanel::BuildResult CriteriaPanel::buildQuery() const {
    BuildResult result;
    for (const Row& row : m_rows) {
        if (!row.available) continue;
        const paintdb::AttributeDef& def = *row.def;
        paintmatch::Criterion c;

        if (row.target) {
            if (auto t = row.target->optionalValue()) c.target = paintmatch::number(*t);
            c.min = row.min->optionalValue();
            c.max = row.max->optionalValue();
            if (c.min && c.max && *c.min > *c.max)
                result.errors << tr("%1: the minimum (%2) is greater than the maximum (%3).")
                                     .arg(toQ(def.label), formatNumber(def, *c.min), formatNumber(def, *c.max));
        } else if (row.combo) {
            const QString v = row.combo->currentData().toString();
            if (!v.isEmpty()) c.target = paintmatch::text(toStd(v));
        } else if (row.multi) {
            const QStringList checked = row.multi->checkedItems();
            if (!checked.isEmpty()) {
                std::vector<std::string> items;
                for (const QString& s : checked) items.push_back(toStd(s));
                c.target = paintmatch::items(std::move(items));
            }
        } else if (row.text) {
            const QString t = row.text->text().trimmed();
            if (!t.isEmpty()) c.target = paintmatch::text(toStd(t));
        }

        c.required = row.required->isChecked() && !paintmatch::isEmpty(c.target);
        c.weight = row.weight->value();
        if (c.isActive()) result.query.criteria[def.key] = c;
    }
    return result;
}

QString CriteriaPanel::criterionSummary(std::size_t attr) const {
    if (attr >= m_rows.size()) return {};
    const Row& row = m_rows[attr];
    const paintdb::AttributeDef& def = *row.def;
    QStringList parts;

    if (row.target) {
        if (auto t = row.target->optionalValue()) parts << formatNumber(def, *t);
        if (auto mn = row.min->optionalValue()) parts << tr("min %1").arg(formatNumber(def, *mn));
        if (auto mx = row.max->optionalValue()) parts << tr("max %1").arg(formatNumber(def, *mx));
    } else if (row.combo && row.combo->currentIndex() > 0) {
        parts << row.combo->currentText();
    } else if (row.multi && !row.multi->checkedItems().isEmpty()) {
        parts << row.multi->checkedItems().join(QStringLiteral(", "));
    } else if (row.text && !row.text->text().trimmed().isEmpty()) {
        parts << QStringLiteral("\"%1\"").arg(row.text->text().trimmed());
    }
    if (row.required->isChecked()) parts << tr("required");
    return parts.join(QStringLiteral(" · "));  // middle dot
}
