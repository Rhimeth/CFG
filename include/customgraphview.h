#ifndef CUSTOMGRAPHVIEW_H
#define CUSTOMGRAPHVIEW_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QMap>
#include <QJsonObject>
class CustomGraphView : public QGraphicsView {
    Q_OBJECT

enum class LayoutAlgorithm {
    Hierarchical,
    ForceDirected,
    Circular
};

public:
    explicit CustomGraphView(QWidget *parent = nullptr);
    ~CustomGraphView() override;
    
    bool hasHighlightedItems() const;

    // Node management
    void addNode(const std::string& id, const std::string& label);
    void addNode(const QString& id, const QString& label, bool isNewFile = false);
    void addEdge(const QString& from, const QString& to);
    void applyHierarchicalLayout();
    void applyForceDirectedLayout(int iterations = 50, 
                                double repulsion = 6000.0,
                                double attraction = 0.06,
                                double maxDisplacement = 30.0);
    void applyCircularLayout();
    void clear();

    void fitView();
    void paintEvent(QPaintEvent* event);
    
    // Visualization features
    bool parseDotFormat(const QString& dotContent);
    void highlightFunction(const QString& functionName);
    void addFunctionCallHierarchy(const QJsonObject& functionCalls);
    void parsePlainFormat(const QString& plainOutput);
    void parseJson(const QByteArray &jsonData);
    void displayMergedGraph(const QJsonObject &mergedGraph);
    void showEvent(QShowEvent *event);
    void toggleGraphDisplay(bool showFullGraph);
    void setNodeLabelsVisible(bool visible);
    void setEdgeLabelsVisible(bool visible);
    void setLayoutAlgorithm(LayoutAlgorithm algorithm);

    QMap<QString, QString> parseAttributes(const QString& attrStr);
    QGraphicsItem* findNodeById(int id);

    void setScene(QGraphicsScene* scene) {
        m_scene = scene;
        QGraphicsView::setScene(scene);
    }
    
    // Theme and creation
    void setThemeColors(const QColor& nodeColor, const QColor& edgeColor, const QColor& textColor);
    void createNode(const QPointF& position);
    void createEdge(QGraphicsItem* source, QGraphicsItem* target);
    
    // View manipulation
    void zoomIn();
    void zoomOut();
    void resetZoom();

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QGraphicsScene* m_scene;
    QMap<QString, QGraphicsEllipseItem*> m_nodes;
    QList<QPair<QString, QString>> m_edges;
    QMap<QString, int> m_nodeLevels;
    double m_zoomFactor;
    bool m_panning;
    QPoint m_panStart;
    bool m_initialized = false;
    QTimer* m_initTimer;

    QMap<int, QGraphicsItem*> m_nodesMap;
    void parseAndCreateNode(int id, const QString& label, const QMap<QString, QString>& attributes);
    void parseAndCreateEdge(int sourceId, int targetId, const QMap<QString, QString>& attributes);

    // Private helper methods
    void calculateLevels();  // Removed duplicate declaration
    void createNodeFromDot(int id, const QString& label, const QMap<QString, QString>& attributes);
    void createEdgeFromDot(int source, int target, const QMap<QString, QString>& attributes);
    QGraphicsTextItem* createNodeItem(const QString& label, bool isNewFile = false);
    void layoutNodes();
};

#endif // CUSTOMGRAPHVIEW_H