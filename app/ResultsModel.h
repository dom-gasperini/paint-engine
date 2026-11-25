#pragma once

#include "paint_match.hpp"
#include "paint_schema.hpp"

#include <QAbstractTableModel>
#include <QColor>
#include <QStringList>
#include <QStyledItemDelegate>

#include <optional>

// Ranked hits: one row per product, fixed columns (rank, match %, product, coating type)
// followed by one column per attribute used in the search. Attribute cells show the
// product's value and its similarity, tinted from red (0%) to green (100%).
class ResultsModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { RankColumn, MatchColumn, ProductColumn, TypeColumn, FirstAttributeColumn };
    enum Role { ScoreRole = Qt::UserRole + 1 };

    explicit ResultsModel(QObject* parent = nullptr);

    // `db` must outlive the results (call clear() before replacing the database).
    // `attributeHeaders` holds one header per entry of result.activeAttributes.
    void setResults(const paintdb::Database* db, const paintmatch::SearchResult& result,
                    const QStringList& attributeHeaders);
    void clear();

    const paintmatch::Match* matchAt(int row) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    static QColor scoreColor(double similarity);  // pastel red -> green

private:
    const paintdb::Database* m_db = nullptr;
    paintmatch::SearchResult m_result;
    QStringList m_attributeHeaders;
};

// Draws the match percentage as a horizontal bar.
class MatchBarDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
