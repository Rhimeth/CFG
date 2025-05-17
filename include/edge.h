#ifndef EDGE_H
#define EDGE_H

#include <QString>
#include <QPointF>
#include <QGraphicsItem>

class GraphicalCFGNode; // Forward declaration

class Edge : public QGraphicsItem {
public:
    // Constructors
    Edge();
    Edge(const QString& source, const QString& destination);
    Edge(const QPointF& sourcePos, const QPointF& destPos);
    Edge(GraphicalCFGNode* fromNode, GraphicalCFGNode* toNode);

    // QGraphicsItem required methods
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // Getters
    QString getSourceNode() const;
    QString getDestinationNode() const;
    QPointF getSourcePosition() const;
    QPointF getDestinationPosition() const;

    // Setters
    void setSourceNode(const QString& source);
    void setDestinationNode(const QString& destination);
    void setSourcePosition(const QPointF& pos);
    void setDestinationPosition(const QPointF& pos);

    // Additional utility methods
    double getLength() const;
    bool isValid() const;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
    QString sourceNode;
    QString destinationNode;
    QPointF sourcePosition;
    QPointF destinationPosition;
    GraphicalCFGNode* m_fromNode;
    GraphicalCFGNode* m_toNode;
};

#endif // EDGE_H