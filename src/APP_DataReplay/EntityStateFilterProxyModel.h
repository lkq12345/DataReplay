/**
 * @file EntityStateFilterProxyModel.h
 * @brief 实体状态表过滤代理模型
 *
 * 在实体状态表的 QStandardItemModel 与 QTableView 之间插入代理层，
 * 实现按 实体ID（列0）、名称（列1）两列的模糊匹配过滤。
 * 属性列（X/Y/Z 等）不参与搜索。
 */

#ifndef ENTITYSTATEFILTERPROXYMODEL_H
#define ENTITYSTATEFILTERPROXYMODEL_H

#include <QSortFilterProxyModel>

/**
 * @brief 实体状态表多列模糊过滤代理模型
 *
 * 重写 filterAcceptsRow：检查 实体ID（列0）、名称（列1）中任意一列
 * 是否包含搜索关键词（大小写不敏感）。搜索词为空时显示全部行。
 */
class EntityStateFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit EntityStateFilterProxyModel(QObject *parent = nullptr);

protected:
    /** @brief 判断指定行是否匹配过滤条件（实体ID/名称 模糊匹配） */
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
};

#endif // ENTITYSTATEFILTERPROXYMODEL_H
