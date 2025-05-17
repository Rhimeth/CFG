#include "graph_viewer.h"
#include <QRegularExpression>
#include <QMouseEvent>
#include <QToolTip>
#include <QDebug>

GraphViewer::GraphViewer(QWidget* parent) 
    : QGraphicsView(parent), m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
}

GraphViewer::~GraphViewer() {
}

void GraphViewer::loadFromDot(const QString& dotContent) {
    m_scene->clear();
    m_nodes.clear();
    m_edges.clear();
    parseDot(dotContent);
}

void GraphViewer::parseDot(const QString& content) {
    QRegularExpression nodeRe(R"((\w+)\s*\[(.*)\])");
    QRegularExpression edgeRe(R"((\w+)\s*->\s*(\w+)\s*\[(.*)\])");
    QRegularExpression attrRe(R"((\w+)=([^,\]]+))");

    for (const QString& line : content.split('\n')) {
        // Parse nodes
        auto nodeMatch = nodeRe.match(line);
        if (nodeMatch.hasMatch()) {
            QMap<QString, QVariant> attrs;
            QString attrStr = nodeMatch.captured(2);
            auto attrIter = attrRe.globalMatch(attrStr);
            while (attrIter.hasNext()) {
                auto match = attrIter.next();
                attrs[match.captured(1)] = match.captured(2).remove('"');
            }
            addNode(nodeMatch.captured(1), attrs);
        }

        auto edgeMatch = edgeRe.match(line);
        if (edgeMatch.hasMatch()) {
            QMap<QString, QVariant> attrs;
            QString attrStr = edgeMatch.captured(3);
            auto attrIter = attrRe.globalMatch(attrStr);
            while (attrIter.hasNext()) {
                auto match = attrIter.next();
                attrs[match.captured(1)] = match.captured(2).remove('"');
            }
            addEdge(edgeMatch.captured(1), edgeMatch.captured(2), attrs);
        }
    }
}

void GraphViewer::addNode(const QString& id, const QMap<QString,QVariant>& attrs) {
    NodeItem item;
    item.shape = new QGraphicsEllipseItem(-20, -20, 40, 40);
    
    // Apply attributes
    if (attrs.contains("fillcolor")) {
        item.shape->setBrush(QColor(attrs["fillcolor"].toString()));
    }
    if (attrs.contains("color")) {
        item.shape->setPen(QPen(QColor(attrs["color"].toString()), 2));
    }
    
    item.label = new QGraphicsTextItem(attrs.contains("label") ? 
                                     attrs["label"].toString() : id);
    item.label->setPos(-15, -10);
    
    m_scene->addItem(item.shape);
    m_scene->addItem(item.label);
    m_nodes[id] = item;
}

void GraphViewer::addEdge(const QString& from, const QString& to, 
                         const QMap<QString,QVariant>& attrs) {
    if (!m_nodes.contains(from) || !m_nodes.contains(to)) return;
    
    auto line = new QGraphicsLineItem();
    QPointF fromPos = m_nodes[from].shape->scenePos();
    QPointF toPos = m_nodes[to].shape->scenePos();
    line->setLine(QLineF(fromPos, toPos));
    
    if (attrs.contains("color")) {
        line->setPen(QPen(QColor(attrs["color"].toString()), 2));
    }
    if (attrs.contains("style") && attrs["style"].toString() == "dashed") {
        QPen pen = line->pen();
        pen.setStyle(Qt::DashLine);
        line->setPen(pen);
    }
    
    m_scene->addItem(line);
    m_edges[qMakePair(from, to)] = line;
}

void GraphViewer::mousePressEvent(QMouseEvent* event) {
    QGraphicsView::mousePressEvent(event);
    
    if (event->button() == Qt::LeftButton) {
        if (auto item = m_scene->itemAt(mapToScene(event->pos()), QTransform())) {
            for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
                if (it->shape == item || it->label == item) {
                    emit nodeClicked(it.key());
                    return;
                }
            }
        }
    }
}

void GraphViewer::mouseMoveEvent(QMouseEvent* event) {
    QGraphicsView::mouseMoveEvent(event);
    
    if (auto item = m_scene->itemAt(mapToScene(event->pos()), QTransform())) {
        for (auto it = m_edges.begin(); it != m_edges.end(); ++it) {
            if (*it == item) {
                // Fix: access the key (QPair) using it.key() instead of the value
                QPair<QString, QString> edgePair = it.key();
                emit edgeHovered(edgePair.first, edgePair.second);
                return;
            }
        }
    }
}