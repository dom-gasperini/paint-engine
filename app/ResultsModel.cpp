#include "ResultsModel.h"

#include "AppInfo.h"

#include <QApplication>
#include <QPainter>
#include <QStyle>

#include <algorithm>

namespace {

QString percent(double v) { return QStringLiteral("%1%").arg(v * 100.0, 0, 'f', 0); }

}  // namespace

ResultsModel::ResultsModel(QObject* parent) : QAbstractTableModel(parent) {}

void ResultsModel::setResults(const paintdb::Database* db, const paintmatch::SearchResult& result,
                              const QStringList& attributeHeaders) {
    beginResetModel();
    m_db = db;
    m_result = result;
    m_attributeHeaders = attributeHeaders;
    while (m_attributeHeaders.size() < static_cast<qsizetype>(m_result.activeAttributes.size()))
        m_attributeHeaders << QString();
    endResetModel();
}

void ResultsModel::clear() {
    beginResetModel();
    m_db = nullptr;
    m_result = {};
    m_attributeHeaders.clear();
    endResetModel();
}

const paintmatch::Match* ResultsModel::matchAt(int row) const {
    if (row < 0 || row >= static_cast<int>(m_result.matches.size())) return nullptr;
    return &m_result.matches[static_cast<std::size_t>(row)];
}

int ResultsModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !m_db) return 0;
    return static_cast<int>(m_result.matches.size());
}

int ResultsModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return FirstAttributeColumn + static_cast<int>(m_result.activeAttributes.size());
}

QColor ResultsModel::scoreColor(double similarity) {
    const double s = std::clamp(similarity, 0.0, 1.0);
    return QColor::fromHsvF(static_cast<float>(s * 120.0 / 360.0), 0.42f, 0.97f);
}

QVariant ResultsModel::data(const QModelIndex& index, int role) const {
    const paintmatch::Match* match = index.isValid() ? matchAt(index.row()) : nullptr;
    if (!match || !m_db) return {};

    const paintdb::Product& product = m_db->products[match->row];
    const int column = index.column();

    switch (column) {
        case RankColumn:
            if (role == Qt::DisplayRole) return index.row() + 1;
            if (role == Qt::TextAlignmentRole) return int(Qt::AlignCenter);
            return {};

        case MatchColumn:
            if (role == Qt::DisplayRole) return QStringLiteral("%1%").arg(match->score * 100.0, 0, 'f', 1);
            if (role == ScoreRole) return match->score;
            if (role == Qt::TextAlignmentRole) return int(Qt::AlignCenter);
            if (role == Qt::ToolTipRole)
                return tr("Weighted similarity across the %n scored attribute(s)", nullptr,
                          static_cast<int>(std::count_if(match->breakdown.begin(), match->breakdown.end(),
                                                         [](const paintmatch::AttributeScore& a) { return a.scored; })));
            return {};

        case ProductColumn:
            if (role == Qt::DisplayRole) return toQ(product.displayName());
            if (role == Qt::ToolTipRole) return toQ(product.field(paintdb::col::AdvertisedPurpose));
            return {};

        case TypeColumn:
            if (role == Qt::DisplayRole) return toQ(product.field(paintdb::col::CoatingType));
            return {};

        default:
            break;
    }

    const std::size_t k = static_cast<std::size_t>(column - FirstAttributeColumn);
    if (k >= match->breakdown.size()) return {};
    const paintmatch::AttributeScore& as = match->breakdown[k];
    const paintdb::AttributeDef& def = paintdb::attributeDefs()[as.attribute];
    const QString value = toQ(paintdb::formatValue(def, m_db->values[match->row][as.attribute]));

    switch (role) {
        case Qt::DisplayRole: {
            const QString shown = value.isEmpty() ? QStringLiteral("—") : value;
            if (!as.scored) return shown;
            return QStringLiteral("%1  (%2)").arg(shown, percent(as.similarity));
        }
        case Qt::BackgroundRole:
            if (as.scored) return scoreColor(as.similarity);
            return {};
        case Qt::ForegroundRole:
            if (as.scored) return QColor(Qt::black);  // readable on the pastel tint in dark themes too
            return {};
        case Qt::ToolTipRole: {
            QString tip = QStringLiteral("<b>%1</b><br>%2").arg(toQ(def.label).toHtmlEscaped(),
                                                               value.isEmpty() ? tr("No value in the database")
                                                                               : value.toHtmlEscaped());
            if (as.scored) tip += tr("<br>Similarity: %1").arg(percent(as.similarity));
            else tip += tr("<br>Filter only (not scored)");
            if (!as.hasValue && as.scored) tip += tr("<br><i>Missing value: scored as a partial match.</i>");
            return tip;
        }
        default:
            return {};
    }
}

QVariant ResultsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal) return QAbstractTableModel::headerData(section, orientation, role);

    if (role == Qt::DisplayRole) {
        switch (section) {
            case RankColumn: return QStringLiteral("#");
            case MatchColumn: return tr("Match");
            case ProductColumn: return tr("Product");
            case TypeColumn: return tr("Coating Type");
            default: break;
        }
        const int k = section - FirstAttributeColumn;
        if (k < 0 || k >= static_cast<int>(m_result.activeAttributes.size())) return {};
        const auto& def = paintdb::attributeDefs()[m_result.activeAttributes[static_cast<std::size_t>(k)]];
        const QString target = m_attributeHeaders.value(k);
        return target.isEmpty() ? toQ(def.label) : toQ(def.label) + QLatin1Char('\n') + target;
    }
    if (role == Qt::ToolTipRole && section >= FirstAttributeColumn) {
        const int k = section - FirstAttributeColumn;
        if (k >= static_cast<int>(m_result.activeAttributes.size())) return {};
        const auto& def = paintdb::attributeDefs()[m_result.activeAttributes[static_cast<std::size_t>(k)]];
        return toQ(def.label) + QStringLiteral(": ") + m_attributeHeaders.value(k);
    }
    return QAbstractTableModel::headerData(section, orientation, role);
}

// ------------------------------------------------------------------ MatchBarDelegate

void MatchBarDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    const QString text = opt.text;
    opt.text.clear();

    const QWidget* widget = opt.widget;
    QStyle* style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);  // background + selection

    const double score = std::clamp(index.data(ResultsModel::ScoreRole).toDouble(), 0.0, 1.0);
    const QRectF bar = QRectF(opt.rect).adjusted(4, 3, -4, -3);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(230, 230, 230));
    painter->drawRoundedRect(bar, 3, 3);
    QRectF fill = bar;
    fill.setWidth(bar.width() * score);
    painter->setBrush(ResultsModel::scoreColor(score).darker(112));
    painter->drawRoundedRect(fill, 3, 3);
    painter->setPen(Qt::black);
    QFont f = opt.font;
    f.setBold(true);
    painter->setFont(f);
    painter->drawText(bar, Qt::AlignCenter, text);
    painter->restore();
}

QSize MatchBarDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QSize s = QStyledItemDelegate::sizeHint(option, index);
    s.setWidth(std::max(s.width() + 24, 90));
    return s;
}
