/**
 * @file ScenarioFilterProxyModel.h
 * @brief 想定文件树过滤代理模型
 *
 * 在 QStandardItemModel 与 QTreeView 之间插入代理层，
 * 实现按节点名称（含子节点）的递归过滤。
 * 父节点在任一子节点匹配时保持可见，避免搜索时丢失层级结构。
 */

#ifndef SCENARIOFILTERPROXYMODEL_H
#define SCENARIOFILTERPROXYMODEL_H

#include <QSortFilterProxyModel>

/**
 * @brief 想定文件树递归过滤代理模型
 *
 * 继承 QSortFilterProxyModel，重写 filterAcceptsRow 实现：
 * - 自身文本包含过滤字符串 → 接受
 * - 任一子节点（递归）包含过滤字符串 → 接受（保证层级可见）
 * - 过滤字符串为空 → 全部接受
 */
class ScenarioFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit ScenarioFilterProxyModel(QObject *parent = nullptr);

protected:
    /** @brief 递归判断行及其子节点是否匹配过滤条件 */
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
};

#endif // SCENARIOFILTERPROXYMODEL_H
