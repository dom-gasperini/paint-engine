#include "InputWidgets.h"

#include <QAbstractItemView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStandardItemModel>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QWheelEvent>

#include <algorithm>

// ------------------------------------------------------------------ NoWheelDoubleSpinBox

NoWheelDoubleSpinBox::NoWheelDoubleSpinBox(QWidget* parent) : QDoubleSpinBox(parent) {
    setFocusPolicy(Qt::StrongFocus);
}

void NoWheelDoubleSpinBox::wheelEvent(QWheelEvent* event) {
    if (!hasFocus()) {
        event->ignore();  // let the scroll area scroll
        return;
    }
    QDoubleSpinBox::wheelEvent(event);
}

// ------------------------------------------------------------------ OptionalSpinBox

OptionalSpinBox::OptionalSpinBox(double minimum, double maximum, double step, int decimals, const QString& noneText,
                                 QWidget* parent)
    : NoWheelDoubleSpinBox(parent), m_minimum(minimum), m_sentinel(minimum - step) {
    setDecimals(decimals);  // before setRange: the range is rounded to the decimals
    setSingleStep(step);
    setRange(m_sentinel, maximum);
    setSpecialValueText(noneText);  // shown while value() == minimum() == sentinel
    setAccelerated(true);
    setValue(m_sentinel);
    connect(this, &QDoubleSpinBox::valueChanged, this, &OptionalSpinBox::optionalValueChanged);
}

std::optional<double> OptionalSpinBox::optionalValue() const {
    const double v = value();
    if (v <= minimum() + 1e-9) return std::nullopt;
    return std::max(v, m_minimum);  // a typed value between sentinel and minimum counts as minimum
}

void OptionalSpinBox::setOptionalValue(std::optional<double> v) {
    setValue(v ? std::max(*v, m_minimum) : minimum());
}

// ------------------------------------------------------------------ NoWheelComboBox

NoWheelComboBox::NoWheelComboBox(QWidget* parent) : QComboBox(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(12);
}

void NoWheelComboBox::wheelEvent(QWheelEvent* event) {
    if (!hasFocus()) {
        event->ignore();
        return;
    }
    QComboBox::wheelEvent(event);
}

// ------------------------------------------------------------------ CheckableComboBox

CheckableComboBox::CheckableComboBox(const QString& placeholder, QWidget* parent)
    : NoWheelComboBox(parent), m_model(new QStandardItemModel(this)), m_placeholder(placeholder) {
    setModel(m_model);
    view()->viewport()->installEventFilter(this);
    view()->installEventFilter(this);
    connect(m_model, &QStandardItemModel::itemChanged, this, [this] {
        if (m_bulkUpdate) return;
        refresh();
        emit checkedItemsChanged();
    });
    refresh();
}

void CheckableComboBox::setItems(const QStringList& items) {
    const QStringList previouslyChecked = checkedItems();
    m_bulkUpdate = true;
    m_model->clear();
    for (const QString& text : items) {
        auto* item = new QStandardItem(text);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(previouslyChecked.contains(text, Qt::CaseInsensitive) ? Qt::Checked : Qt::Unchecked);
        m_model->appendRow(item);
    }
    m_bulkUpdate = false;
    refresh();
    if (checkedItems() != previouslyChecked) emit checkedItemsChanged();
}

QStringList CheckableComboBox::checkedItems() const {
    QStringList out;
    for (int r = 0; r < m_model->rowCount(); ++r) {
        const QStandardItem* item = m_model->item(r);
        if (item && item->checkState() == Qt::Checked) out << item->text();
    }
    return out;
}

void CheckableComboBox::setCheckedItems(const QStringList& items) {
    const QStringList before = checkedItems();
    m_bulkUpdate = true;
    for (int r = 0; r < m_model->rowCount(); ++r) {
        QStandardItem* item = m_model->item(r);
        if (item) item->setCheckState(items.contains(item->text(), Qt::CaseInsensitive) ? Qt::Checked : Qt::Unchecked);
    }
    m_bulkUpdate = false;
    refresh();
    if (checkedItems() != before) emit checkedItemsChanged();
}

bool CheckableComboBox::eventFilter(QObject* watched, QEvent* event) {
    if (watched == view()->viewport() && event->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QModelIndex index = view()->indexAt(me->position().toPoint());
        if (QStandardItem* item = m_model->itemFromIndex(index)) {
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        }
        return true;  // swallow the release so the popup stays open
    }
    if (watched == view() && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Space) {
            if (QStandardItem* item = m_model->itemFromIndex(view()->currentIndex()))
                item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            return true;
        }
    }
    return NoWheelComboBox::eventFilter(watched, event);
}

QString CheckableComboBox::displayText() const {
    const QStringList checked = checkedItems();
    return checked.isEmpty() ? m_placeholder : checked.join(QStringLiteral(", "));
}

void CheckableComboBox::refresh() {
    const QStringList checked = checkedItems();
    setToolTip(checked.isEmpty() ? tr("Click to choose one or more values") : checked.join(QLatin1Char('\n')));
    update();
}

void CheckableComboBox::paintEvent(QPaintEvent*) {
    QStylePainter painter(this);
    painter.setPen(palette().color(QPalette::Text));

    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    const QRect textRect = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
    opt.currentText = fontMetrics().elidedText(displayText(), Qt::ElideRight, std::max(textRect.width(), 10));
    opt.currentIcon = QIcon();

    painter.drawComplexControl(QStyle::CC_ComboBox, opt);
    painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
}
