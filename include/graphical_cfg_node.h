#ifndef GRAPHICAL_CFG_NODE_H
#define GRAPHICAL_CFG_NODE_H

#include <QGraphicsItem>
#include <QPainter>
#include <QColor>
#include <QString>
#include <memory>

// Forward declarations to minimize includes
class CFGNode;
class QGraphicsSceneMouseEvent;

class GraphicalCFGNode : public QGraphicsItem {
public:
    // Constructor for CFG node
    explicit GraphicalCFGNode(const std::shared_ptr<CFGNode>& cfgNode, QGraphicsItem* parent = nullptr);

    // Constructor for general graphical node
    GraphicalCFGNode(const QString& id, const QString& label, bool isNewFile, QGraphicsItem* parent = nullptr);

    // QGraphicsItem required methods
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // Interaction methods
    void setColor(const QColor& color);
    QString getNodeLabel() const;
    QString getNodeId() const;
    bool isNewFile() const;

    // Optional: get underlying CFG node if applicable
    std::shared_ptr<CFGNode> getCFGNode() const { return m_cfgNode; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    std::shared_ptr<CFGNode> m_cfgNode;
    QColor m_color;
    QString m_id;
    QString m_label;
    bool m_isNewFile;

    // Configurable node dimensions
    static const int NODE_WIDTH = 100;
    static const int NODE_HEIGHT = 50;

    // Internal initialization method
    void initializeNode();
};

#endif // GRAPHICAL_CFG_NODE_H