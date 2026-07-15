/**
 * @file ScenarioFilterProxyModel.cpp
 * @brief 想定文件树过滤代理模型的实现
 */

#include "ScenarioFilterProxyModel.h"

ScenarioFilterProxyModel::ScenarioFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    // 大小写不敏感匹配
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    // 过滤角色：显示文本（列 0）
    setFilterRole(Qt::DisplayRole);
}

bool ScenarioFilterProxyModel::filterAcceptsRow(int sourceRow,
                                                 const QModelIndex &sourceParent) const
{
    // 过滤字符串为空 → 全部接受
    if (filterRegularExpression().pattern().isEmpty())
        return true;

    // ① 当前行自身是否匹配
    QModelIndex index = sourceModel()->index(sourceRow, filterKeyColumn(), sourceParent);
    QString text = sourceModel()->data(index, filterRole()).toString();

    if (text.contains(filterRegularExpression()))
        return true;

    // ② 递归检查所有子节点——任一子节点匹配则父节点保持可见
    int childCount = sourceModel()->rowCount(index);
    for (int i = 0; i < childCount; ++i) {
        if (filterAcceptsRow(i, index))
            return true;
    }

    return false;
}
