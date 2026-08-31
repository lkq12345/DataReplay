/**
 * @file EntityStateFilterProxyModel.cpp
 * @brief 实体状态表过滤代理模型的实现
 */

#include "EntityStateFilterProxyModel.h"

EntityStateFilterProxyModel::EntityStateFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
}

bool EntityStateFilterProxyModel::filterAcceptsRow(int sourceRow,
                                                   const QModelIndex &sourceParent) const
{
    // 搜索词为空 → 显示全部
    if (filterRegularExpression().pattern().isEmpty())
        return true;

    // 对 实体ID（列0）、名称（列1）逐一检查模糊匹配，任意一列命中即接受
    for (int col = 0; col <= 1; ++col) {
        QModelIndex index = sourceModel()->index(sourceRow, col, sourceParent);
        QString text = sourceModel()->data(index, Qt::DisplayRole).toString();
        if (text.contains(filterRegularExpression()))
            return true;
    }

    return false;
}
