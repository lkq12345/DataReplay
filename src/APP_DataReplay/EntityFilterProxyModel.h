/**
 * @file EntityFilterProxyModel.h
 * @brief 实体配置表过滤代理模型
 *
 * 在 QStandardItemModel 与 QTableView 之间插入代理层，
 * 实现按 ID / 名称 / 映射ID 三列的模糊匹配过滤。
 */

#ifndef ENTITYFILTERPROXYMODEL_H
#define ENTITYFILTERPROXYMODEL_H

#include <QSortFilterProxyModel>

/**
 * @brief 实体表多列模糊过滤代理模型
 *
 * 重写 filterAcceptsRow：检查 ID（Col=0）、名称（Col=1）、
 * 映射ID（Col=2）中任意一列是否包含搜索关键词（大小写不敏感）。
 * 搜索词为空时显示全部行。
 */
class EntityFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit EntityFilterProxyModel(QObject *parent = nullptr);

protected:
    /** @brief 判断指定行是否匹配过滤条件（多列模糊匹配） */
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
};

#endif // ENTITYFILTERPROXYMODEL_H
