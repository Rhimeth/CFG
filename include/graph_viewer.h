#ifndef GRAPH_VIEWER_H
#define GRAPH_VIEWER_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QMap>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsTextItem>

class GraphViewer : public QGraphicsView {
    Q_OBJECT
public:
    explicit GraphViewer(QWidget* parent = nullptr);
    ~GraphViewer() override;
    
    void loadFromDot(const QString& dotContent);

signals:
    void nodeClicked(const QString& id);
    void edgeHovered(const QString& from, const QString& to);

private:
    struct NodeItem {
        QGraphicsEllipseItem* shape;
        QGraphicsTextItem* label;
    };

    QGraphicsScene* m_scene;
    QMap<QString, NodeItem> m_nodes;
    QMap<QPair<QString, QString>, QGraphicsLineItem*> m_edges;
    
    void parseDot(const QString& content);
    void addNode(const QString& id, const QMap<QString,QVariant>& attrs);
    void addEdge(const QString& from, const QString& to, 
                const QMap<QString,QVariant>& attrs);
    
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
};

#endif // GRAPH_VIEWER_H