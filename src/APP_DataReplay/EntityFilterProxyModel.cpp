/**
 * @file EntityFilterProxyModel.cpp
 * @brief 实体配置表过滤代理模型的实现
 */

#include "EntityFilterProxyModel.h"

EntityFilterProxyModel::EntityFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
}

bool EntityFilterProxyModel::filterAcceptsRow(int sourceRow,
                                              const QModelIndex &sourceParent) const
{
    // 搜索词为空 → 显示全部
    if (filterRegularExpression().pattern().isEmpty())
        return true;

    // 对 ID（列0）、名称（列1）、映射ID（列2）逐一检查模糊匹配
    // 任意一列包含搜索词即接受该行
    for (int col = 0; col <= 2; ++col) {
        QModelIndex index = sourceModel()->index(sourceRow, col, sourceParent);
        QString text = sourceModel()->data(index, Qt::DisplayRole).toString();
        if (text.contains(filterRegularExpression()))
            return true;
    }

    return false;
}
