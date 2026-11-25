#pragma once

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QStringList>

#include <optional>

class QStandardItemModel;

// Spin boxes and combo boxes inside a scroll area should not change value when the user
// is just scrolling the page, so they only react to the mouse wheel when focused.

class NoWheelDoubleSpinBox : public QDoubleSpinBox {
    Q_OBJECT
public:
    explicit NoWheelDoubleSpinBox(QWidget* parent = nullptr);

protected:
    void wheelEvent(QWheelEvent* event) override;
};

// A double spin box with an extra "no value" state shown as `noneText` (e.g. "Any").
class OptionalSpinBox : public NoWheelDoubleSpinBox {
    Q_OBJECT
public:
    OptionalSpinBox(double minimum, double maximum, double step, int decimals, const QString& noneText,
                    QWidget* parent = nullptr);

    std::optional<double> optionalValue() const;
    void setOptionalValue(std::optional<double> value);
    void reset() { setOptionalValue(std::nullopt); }
    bool hasValue() const { return optionalValue().has_value(); }

signals:
    void optionalValueChanged();

private:
    double m_minimum;
    double m_sentinel;
};

class NoWheelComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit NoWheelComboBox(QWidget* parent = nullptr);

protected:
    void wheelEvent(QWheelEvent* event) override;
};

// Combo box whose items have check boxes; the popup stays open while items are toggled.
class CheckableComboBox : public NoWheelComboBox {
    Q_OBJECT
public:
    explicit CheckableComboBox(const QString& placeholder, QWidget* parent = nullptr);

    void setItems(const QStringList& items);  // keeps the check state of items that still exist
    QStringList checkedItems() const;
    void setCheckedItems(const QStringList& items);
    void clearChecks() { setCheckedItems({}); }

signals:
    void checkedItemsChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QString displayText() const;
    void refresh();

    QStandardItemModel* m_model;
    QString m_placeholder;
    bool m_bulkUpdate = false;
};
