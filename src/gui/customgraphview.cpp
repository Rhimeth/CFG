#include "customgraphview.h"
#include "mainwindow.h"
#include <QGraphicsEllipseItem>
#include <QRegExp>
#include <QDebug>
#include <QScrollBar>
#include <QGraphicsTextItem>
#include <QJsonDocument>
#include <QGraphicsLineItem>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QQueue>
#include <QPair>
#include <QTimer>
#include <cmath>
#include <exception>

CustomGraphView::CustomGraphView(QWidget* parent) 
    : QGraphicsView(parent),
      m_scene(nullptr),
      m_zoomFactor(1.0),
      m_panning(false),
      m_initialized(false),
      m_initTimer(nullptr)
{
    setOptimizationFlags(QGraphicsView::DontSavePainterState | 
                       QGraphicsView::DontAdjustForAntialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    
    // Create a new scene
    if (!m_scene) {
        m_scene = new QGraphicsScene(this);
        QGraphicsView::setScene(m_scene);
    }
}
void CustomGraphView::paintEvent(QPaintEvent* event)
{
    static int attempt = 0;
    const int maxAttempts = 2;
    
    try {
        QPainter painter(viewport());
        
        painter.fillRect(viewport()->rect(), Qt::white);
        render(&painter);
        
        attempt = 0;
    } 
    catch (...) {
        if (++attempt >= maxAttempts) {
            qCritical() << "Permanent render failure - disabling view";
            setEnabled(false);
            return;
        }
        
        qDebug() << "Rendering failed, attempt" << attempt;
        setViewport(new QWidget());
        update();
    }
}
QGraphicsTextItem* CustomGraphView::createNodeItem(const QString& label, bool isNewFile) {
    QGraphicsTextItem* nodeItem = m_scene->addText(label);
    
    if (isNewFile) {
        nodeItem->setDefaultTextColor(Qt::blue);
        QFont font = nodeItem->font();
        font.setBold(true);
        nodeItem->setFont(font);
    } else {
        nodeItem->setDefaultTextColor(Qt::black);
    }
    
    return nodeItem;
}

void CustomGraphView::addNode(const QString& id, const QString& label, bool isNewFile)
{
    if (!m_scene) {
        qWarning() << "Attempted to add node to null scene";
        return;
    }

    if (m_nodes.contains(id)) {
        qWarning() << "Node" << id << "already exists";
        return;
    }

    QGraphicsEllipseItem* ellipseItem = m_scene->addEllipse(0, 0, 80, 40, 
                                                          QPen(Qt::black), 
                                                          QBrush(Qt::lightGray));
    
    // Create text label
    QGraphicsTextItem* textItem = createNodeItem(label, isNewFile);
    
    // Position items
    bool ok;
    int numericId = id.toInt(&ok);
    if (ok) {
        int x = (numericId % 5) * 150;
        int y = (numericId / 5) * 100;
        ellipseItem->setPos(x, y);
        textItem->setPos(x + 10, y + 5);
    }
    
    // Store the node
    m_nodes[id] = ellipseItem;
}

void CustomGraphView::showEvent(QShowEvent *event)
{
    QGraphicsView::showEvent(event);
    if (m_scene) {
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
    }
}

void CustomGraphView::setThemeColors(const QColor& nodeColor, const QColor& edgeColor, const QColor& textColor)
{
    if (!scene()) return;
    
    foreach (QGraphicsItem* item, scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            if (auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {
                ellipse->setBrush(nodeColor);
                ellipse->setPen(QPen(edgeColor, 1));
            }
            // Update text items
            foreach (QGraphicsItem* child, item->childItems()) {
                if (auto text = dynamic_cast<QGraphicsTextItem*>(child)) {
                    text->setDefaultTextColor(textColor);
                }
            }
        }
        else if (item->data(MainWindow::EdgeItemType).toInt() == 1) {
            if (auto line = dynamic_cast<QGraphicsLineItem*>(item)) {
                QPen pen = line->pen();
                pen.setColor(edgeColor);
                line->setPen(pen);
            }
        }
    }
}

void CustomGraphView::createNode(const QPointF& position)
{
    QGraphicsEllipseItem* nodeItem = new QGraphicsEllipseItem(-15, -15, 30, 30);
    nodeItem->setPos(position);
    nodeItem->setData(MainWindow::NodeItemType, 1);
    scene()->addItem(nodeItem);
    
    // Add text label
    QGraphicsTextItem* textItem = new QGraphicsTextItem("Node");
    textItem->setParentItem(nodeItem);
    textItem->setPos(-10, -10);
}

void CustomGraphView::createEdge(QGraphicsItem* source, QGraphicsItem* target)
{
    QLineF line(source->sceneBoundingRect().center(), 
               target->sceneBoundingRect().center());
    QGraphicsLineItem* edgeItem = new QGraphicsLineItem(line);
    edgeItem->setData(MainWindow::EdgeItemType, 1);
    scene()->addItem(edgeItem);
}

void CustomGraphView::addEdge(const QString& from, const QString& to) {
    qDebug() << "Adding edge:" << from << "to" << to;
    
    if (m_nodes.contains(from) && m_nodes.contains(to)) {
        QGraphicsEllipseItem* fromItem = m_nodes[from];
        QGraphicsEllipseItem* toItem = m_nodes[to];
        
        QPointF fromCenter = fromItem->rect().center() + fromItem->pos();
        QPointF toCenter = toItem->rect().center() + toItem->pos();
        
        QGraphicsLineItem* edge = m_scene->addLine(fromCenter.x(), fromCenter.y(), 
                          toCenter.x(), toCenter.y(), 
                          QPen(Qt::black, 1.5));
        edge->setData(0, "edge");  // Mark as edge
        edge->setData(1, from);    // Store source node ID
        edge->setData(2, to);      // Store target node ID
        
        m_edges.append(qMakePair(from, to));
    }
}

void CustomGraphView::calculateLevels() {
    m_nodeLevels.clear();
    
    if (m_nodes.isEmpty()) return;
    
    // Find root nodes
    QSet<QString> rootNodes;
    QSet<QString> allNodes(m_nodes.keys().begin(), m_nodes.keys().end());
    QSet<QString> nodesWithIncomingEdges;
    
    foreach (const auto& edge, m_edges) {
        nodesWithIncomingEdges.insert(edge.second);
    }
    
    // Root nodes are those with no incoming edges
    foreach (const QString& nodeId, allNodes) {
        if (!nodesWithIncomingEdges.contains(nodeId)) {
            rootNodes.insert(nodeId);
        }
    }
    
    if (rootNodes.isEmpty()) {
        rootNodes.insert(*allNodes.begin());
    }
    
    QQueue<QPair<QString, int>> queue;  // (nodeId, level)
    
    foreach (const QString& root, rootNodes) {
        queue.enqueue(qMakePair(root, 0));
    }
    
    while (!queue.isEmpty()) {
        auto current = queue.dequeue();
        QString currentNode = current.first;
        int currentLevel = current.second;
        
        // Update level if we found a longer path
        if (!m_nodeLevels.contains(currentNode) || 
            m_nodeLevels[currentNode] < currentLevel) {
            m_nodeLevels[currentNode] = currentLevel;
        }
        
        // Process all outgoing edges
        foreach (const auto& edge, m_edges) {
            if (edge.first == currentNode) {
                queue.enqueue(qMakePair(edge.second, currentLevel + 1));
            }
        }
    }
    
    foreach (const QString& nodeId, allNodes) {
        if (!m_nodeLevels.contains(nodeId)) {
            m_nodeLevels[nodeId] = 0;
        }
    }
}

void CustomGraphView::applyHierarchicalLayout() {
    const int LEVEL_HEIGHT = 100;
    const int NODE_WIDTH = 120;
    
    // Calculate node levels first
    calculateLevels();
    
    if (m_nodeLevels.isEmpty()) return;
    
    // Count nodes per level
    QMap<int, int> levelCounts;
    foreach (int level, m_nodeLevels.values()) {
        levelCounts[level]++;
    }
    
    // Position nodes
    QMap<int, int> levelPositions;
    foreach (const QString& id, m_nodes.keys()) {
        int level = m_nodeLevels[id];
        int pos = levelPositions[level]++;
        
        qreal x = (pos - (levelCounts[level] - 1) / 2.0) * NODE_WIDTH;
        qreal y = level * LEVEL_HEIGHT;
        
        m_nodes[id]->setPos(x, y);
    }
}

void CustomGraphView::applyForceDirectedLayout(int iterations, 
                                             double repulsion, 
                                             double attraction,
                                             double maxDisplacement)
{
    if (!m_scene || m_nodes.isEmpty()) return;

    QMap<QString, QPointF> positions;
    QMap<QString, QPointF> forces;
    
    // Initialize positions
    foreach (const QString& id, m_nodes.keys()) {
        positions[id] = m_nodes[id]->pos();
    }
    
    for (int iter = 0; iter < iterations; ++iter) {
        forces.clear();
        
        foreach (const QString& id1, m_nodes.keys()) {
            foreach (const QString& id2, m_nodes.keys()) {
                if (id1 == id2) continue;
                
                QPointF delta = positions[id1] - positions[id2];
                qreal distance = std::max(1.0, std::sqrt(delta.x()*delta.x() + delta.y()*delta.y()));
                qreal force = repulsion / (distance * distance);
                
                forces[id1] += (delta / distance) * force;
            }
        }
        
        foreach (const auto& edge, m_edges) {
            QString fromId = edge.first;
            QString toId = edge.second;
            
            if (!positions.contains(fromId) || !positions.contains(toId)) continue;
            
            QPointF delta = positions[fromId] - positions[toId];
            
            forces[fromId] -= delta * attraction;
            forces[toId] += delta * attraction;
        }
        
        // Update positions with limits
        foreach (const QString& id, m_nodes.keys()) {
            qreal displacement = std::sqrt(forces[id].x()*forces[id].x() + 
                                 forces[id].y()*forces[id].y());
            if (displacement > 0) {
                qreal scale = std::min(maxDisplacement, displacement) / displacement;
                positions[id] += forces[id] * scale;
            }
        }
    }
    
    // Apply new positions
    foreach (const QString& id, m_nodes.keys()) {
        m_nodes[id]->setPos(positions[id]);
    }
}

void CustomGraphView::applyCircularLayout() {
    const qreal RADIUS = 200;
    const qreal CENTER_X = 0;
    const qreal CENTER_Y = 0;
    
    int count = m_nodes.size();
    if (count == 0) return;
    
    int i = 0;
    foreach (const QString& id, m_nodes.keys()) {
        qreal angle = 2.0 * M_PI * i / count;
        qreal x = CENTER_X + RADIUS * std::cos(angle);
        qreal y = CENTER_Y + RADIUS * std::sin(angle);
        
        m_nodes[id]->setPos(x, y);
        i++;
    }
}

void CustomGraphView::parsePlainFormat(const QString& plainOutput) {
    clear();
    
    QStringList lines = plainOutput.split('\n', Qt::SkipEmptyParts);
    QMap<QString, QGraphicsEllipseItem*> tempNodes;

    for (const QString& line : lines) {
        QStringList parts = line.split(' ');
        
        if (parts[0] == "node" && parts.size() >= 7) {
            QString nodeId = parts[1];
            double x = parts[2].toDouble() * 100;
            double y = parts[3].toDouble() * 100;
            QString label = parts[6];

            QGraphicsEllipseItem* nodeItem = m_scene->addEllipse(x, y, 80, 40, QPen(Qt::black), QBrush(Qt::lightGray));
            QGraphicsTextItem* textItem = m_scene->addText(label);
            textItem->setPos(x + 10, y + 5);

            tempNodes[nodeId] = nodeItem;
            m_nodes[nodeId] = nodeItem; // Store in main nodes map
        } 
        else if (parts[0] == "edge" && parts.size() >= 3) {
            QString fromNode = parts[1];
            QString toNode = parts[2];

            if (tempNodes.contains(fromNode) && tempNodes.contains(toNode)) {
                QGraphicsEllipseItem* fromItem = tempNodes[fromNode];
                QGraphicsEllipseItem* toItem = tempNodes[toNode];

                QPointF fromCenter = fromItem->rect().center() + fromItem->pos();
                QPointF toCenter = toItem->rect().center() + toItem->pos();

                QGraphicsLineItem* edge = m_scene->addLine(fromCenter.x(), fromCenter.y(),
                                toCenter.x(), toCenter.y(),
                                QPen(Qt::black, 1.5));
                edge->setData(0, "edge");  // Mark as edge
                edge->setData(1, fromNode); // Store source
                edge->setData(2, toNode);   // Store target
                m_edges.append(qMakePair(fromNode, toNode)); // Store relationship
            }
        }
    }
    
    m_scene->setSceneRect(m_scene->itemsBoundingRect());
}

bool CustomGraphView::parseDotFormat(const QString& dotContent) {
    if (!scene()) {
        qWarning() << "No scene available for parsing";
        return false;
    }
    
    // Clear the existing graph before parsing
    clear();
    
    QRegularExpression nodeRegex("\\s*(\\d+|\"[^\"]+\")\\s*\\[\\s*([^\\]]+)\\]\\s*;?");
    
    QRegularExpression edgeRegex("\\s*(\\d+|\"[^\"]+\")\\s*->\\s*(\\d+|\"[^\"]+\")\\s*(?:\\[\\s*([^\\]]+)\\])?\\s*;?");
    
    QRegularExpression attrRegex("(\\w+)\\s*=\\s*(?:\"([^\"]*)\"|([^\\s,;\"]+))");
    
    QRegularExpression globalAttrRegex("\\s*(graph|node|edge)\\s*\\[\\s*([^\\]]+)\\]\\s*;?");
    
    QMap<QString, QMap<QString, QString>> defaultAttributes;
    defaultAttributes["node"]["shape"] = "ellipse";
    defaultAttributes["node"]["style"] = "filled";
    defaultAttributes["node"]["fillcolor"] = "lightgray";
    defaultAttributes["edge"]["color"] = "black";
    
    if (!nodeRegex.isValid() || !edgeRegex.isValid() || !attrRegex.isValid() || !globalAttrRegex.isValid()) {
        qCritical() << "Invalid regular expression patterns";
        return false;
    }
    
    bool parsedSuccessfully = false;
    QStringList lines = dotContent.split('\n', Qt::SkipEmptyParts);
    
    qDebug() << "DOT content sample:" << dotContent.left(200);
    
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        
        // Skip comments and graph declarations
        if (trimmed.startsWith("//") || trimmed.startsWith("#") || trimmed.isEmpty() ||
            trimmed.startsWith("digraph") || trimmed.startsWith("graph") || 
            trimmed == "{" || trimmed == "}") {
            qDebug() << "Skipping line:" << trimmed;
            continue;
        }
        
        // Check for global attribute settings
        QRegularExpressionMatch globalAttrMatch = globalAttrRegex.match(trimmed);
        if (globalAttrMatch.hasMatch()) {
            QString elementType = globalAttrMatch.captured(1); // node, edge, or graph
            QString attrStr = globalAttrMatch.captured(2);
            
            // Parse the attributes
            QRegularExpressionMatchIterator attrMatches = attrRegex.globalMatch(attrStr);
            while (attrMatches.hasNext()) {
                QRegularExpressionMatch match = attrMatches.next();
                QString key = match.captured(1);
                QString value = match.captured(2);
                if (value.isEmpty()) value = match.captured(3); // Non-quoted value
                defaultAttributes[elementType][key] = value;
            }
            
            qDebug() << "Set default" << elementType << "attributes:" << defaultAttributes[elementType];
            parsedSuccessfully = true;
            continue;
        }
        
        QRegularExpressionMatch nodeMatch = nodeRegex.match(trimmed);
        if (nodeMatch.hasMatch()) {
            QString idStr = nodeMatch.captured(1);
            // Strip quotes if present
            if (idStr.startsWith("\"") && idStr.endsWith("\"")) {
                idStr = idStr.mid(1, idStr.length() - 2);
            }
            int id = idStr.toInt();
            
            QString attrStr = nodeMatch.captured(2);
            
            // Parse attributes, starting with defaults
            QMap<QString, QString> attributes = defaultAttributes["node"];
            QRegularExpressionMatchIterator attrMatches = attrRegex.globalMatch(attrStr);
            while (attrMatches.hasNext()) {
                QRegularExpressionMatch match = attrMatches.next();
                QString key = match.captured(1);
                QString value = match.captured(2);
                if (value.isEmpty()) value = match.captured(3);
                attributes[key] = value;
            }
            
            // Get label or use node ID as label
            QString label = attributes.value("label", idStr);
            if (label.startsWith("\"") && label.endsWith("\"")) {
                label = label.mid(1, label.length() - 2);
            }
            
            createNodeFromDot(id, label, attributes);
            qDebug() << "Created node:" << id << "with label:" << label;
            parsedSuccessfully = true;
            continue;
        }
        
        // Try to match edge pattern
        QRegularExpressionMatch edgeMatch = edgeRegex.match(trimmed);
        if (edgeMatch.hasMatch()) {
            QString sourceStr = edgeMatch.captured(1);
            QString targetStr = edgeMatch.captured(2);
            
            // Strip quotes if present
            if (sourceStr.startsWith("\"") && sourceStr.endsWith("\"")) {
                sourceStr = sourceStr.mid(1, sourceStr.length() - 2);
            }
            if (targetStr.startsWith("\"") && targetStr.endsWith("\"")) {
                targetStr = targetStr.mid(1, targetStr.length() - 2);
            }
            
            int source = sourceStr.toInt();
            int target = targetStr.toInt();
            
            QString attrStr = edgeMatch.captured(3);
            
            // Parse attributes, starting with defaults
            QMap<QString, QString> attributes = defaultAttributes["edge"];
            if (!attrStr.isEmpty()) {
                QRegularExpressionMatchIterator attrMatches = attrRegex.globalMatch(attrStr);
                while (attrMatches.hasNext()) {
                    QRegularExpressionMatch match = attrMatches.next();
                    QString key = match.captured(1);
                    QString value = match.captured(2);
                    if (value.isEmpty()) value = match.captured(3); // Non-quoted value
                    attributes[key] = value;
                }
            }
            
            createEdgeFromDot(source, target, attributes);
            qDebug() << "Created edge from" << source << "to" << target;
            parsedSuccessfully = true;
            continue;
        }
        
        qDebug() << "Unmatched line:" << trimmed;
    }
    
    if (parsedSuccessfully) {
        qDebug() << "Successfully parsed DOT content";
        // Update scene bounds
        scene()->setSceneRect(scene()->itemsBoundingRect().adjusted(-20, -20, 20, 20));
    } else {
        qWarning() << "Failed to parse any valid nodes or edges from DOT content";
    }
    
    return parsedSuccessfully;
}

QMap<QString, QString> CustomGraphView::parseAttributes(const QString& attrStr) {
    QMap<QString, QString> attributes;
    
    QRegExp attrRegex(R"(\s*(\w+)\s*=\s*\"([^\"]*)\")");
    int pos = 0;

    while ((pos = attrRegex.indexIn(attrStr, pos)) != -1) {
        QString key = attrRegex.cap(1);
        QString value = attrRegex.cap(2);
        attributes.insert(key, value);
        pos += attrRegex.matchedLength();
    }

    return attributes;
}

void CustomGraphView::createNodeFromDot(int id, const QString& label, const QMap<QString, QString>& attributes) {
    QGraphicsEllipseItem* node = new QGraphicsEllipseItem(-20, -20, 40, 40);
    
    node->setData(MainWindow::NodeItemType, 1);
    node->setData(MainWindow::NodeIdKey, id);
    
    // Apply attributes
    if (attributes.contains("fillcolor")) {
        node->setBrush(QBrush(QColor(attributes["fillcolor"])));
    }
    
    // Add label
    QGraphicsTextItem* text = new QGraphicsTextItem(label, node);
    text->setPos(-15, -15);
    
    scene()->addItem(node);
}

void CustomGraphView::createEdgeFromDot(int source, int target, const QMap<QString, QString>& attributes) {
    QGraphicsItem* sourceItem = findNodeById(source);
    QGraphicsItem* targetItem = findNodeById(target);
    
    if (sourceItem && targetItem) {
        QLineF line(sourceItem->sceneBoundingRect().center(),
                   targetItem->sceneBoundingRect().center());
        QGraphicsLineItem* edge = new QGraphicsLineItem(line);
        
        edge->setData(MainWindow::EdgeItemType, 1);
        edge->setData(MainWindow::EdgeFromKey, source);
        edge->setData(MainWindow::EdgeToKey, target);
        
        // Apply attributes
        if (attributes.contains("color")) {
            edge->setPen(QPen(QColor(attributes["color"])));
        }
        
        scene()->addItem(edge);
    }
}

void CustomGraphView::zoomIn() {
    scale(1.2, 1.2);
    m_zoomFactor *= 1.2;
}

void CustomGraphView::zoomOut() {
    scale(1/1.2, 1/1.2);
    m_zoomFactor /= 1.2;
}

void CustomGraphView::resetZoom() {
    resetTransform();
    m_zoomFactor = 1.0;
}

void CustomGraphView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() > 0) {
            zoomIn();
        } else {
            zoomOut();
        }
        event->accept();
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

void CustomGraphView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStart = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void CustomGraphView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        QScrollBar *hBar = horizontalScrollBar();
        QScrollBar *vBar = verticalScrollBar();
        
        hBar->setValue(hBar->value() - (event->x() - m_panStart.x()));
        vBar->setValue(vBar->value() - (event->y() - m_panStart.y()));
        
        m_panStart = event->pos();
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void CustomGraphView::highlightFunction(const QString& functionName)
{
    // First reset all highlights
    for (auto item : m_scene->items()) {
        if (auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {
            ellipse->setBrush(QBrush(Qt::lightGray));
        }
        else if (auto text = dynamic_cast<QGraphicsTextItem*>(item)) {
            text->setDefaultTextColor(Qt::black);
        }
    }

    // Highlight matching nodes
    for (auto item : m_scene->items()) {
        if (auto text = dynamic_cast<QGraphicsTextItem*>(item)) {
            if (text->toPlainText().contains(functionName, Qt::CaseInsensitive)) {
                text->setDefaultTextColor(Qt::red);
                
                // Find and highlight the corresponding ellipse
                for (auto nearby : m_scene->items(text->pos())) {
                    if (auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(nearby)) {
                        ellipse->setBrush(QBrush(Qt::yellow));
                    }
                }
            }
        }
    }
}

bool CustomGraphView::hasHighlightedItems() const
{
    if (!m_scene) return false;
    
    for (auto item : m_scene->items()) {
        if (auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {
            if (ellipse->brush().color() == Qt::yellow) {
                return true;
            }
        }
    }
    return false;
}

QGraphicsItem* CustomGraphView::findNodeById(int id) {
    foreach(QGraphicsItem* item, scene()->items()) {
        if (item->data(0).toString() == QString::number(id)) {
            return item;
        }
    }
    return nullptr;
}

void CustomGraphView::toggleGraphDisplay(bool showFullGraph) {
    if (!m_scene) return;

    // Store current view center to maintain position
    QPointF oldCenter = mapToScene(viewport()->rect().center());
    
    if (showFullGraph) {
        // FULL VIEW MODE - Show everything
        for (auto item : m_scene->items()) {
            item->setVisible(true);
        }
        
        // Fit entire graph with some padding
        QRectF sceneRect = m_scene->itemsBoundingRect().adjusted(-50, -50, 50, 50);
        fitInView(sceneRect, Qt::KeepAspectRatio);
    } else {
        // SIMPLIFIED VIEW MODE - Show only function nodes
        for (auto item : m_scene->items()) {
            if (dynamic_cast<QGraphicsLineItem*>(item)) {
                item->setVisible(false);  // Hide edges
            } else {
                item->setVisible(true);   // Show nodes
            }
        }
        
        // Center on main function if available
        bool foundMain = false;
        for (auto item : m_scene->items()) {
            if (auto text = dynamic_cast<QGraphicsTextItem*>(item)) {
                if (text->toPlainText().contains("main", Qt::CaseInsensitive)) {
                    centerOn(text);
                    foundMain = true;
                    break;
                }
            }
        }
        
        if (!foundMain) {
            // Fallback: zoom to 75% of full view
            QRectF simplifiedRect = m_scene->itemsBoundingRect();
            simplifiedRect.setSize(simplifiedRect.size() * 0.75);
            fitInView(simplifiedRect, Qt::KeepAspectRatio);
        }
    }
    
    // Restore approximate center position
    centerOn(oldCenter);
    update();
}

void CustomGraphView::parseJson(const QByteArray &jsonData) {
    clear();
    
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isNull()) {
        qWarning() << "Invalid JSON data";
        return;
    }
    
    QJsonObject json = doc.object();
    QMap<QString, QGraphicsEllipseItem*> nodeItems;
    
    // Create nodes
    QJsonArray nodes = json["nodes"].toArray();
    for (const QJsonValue &node : nodes) {
        QJsonObject nodeObj = node.toObject();
        QString id = nodeObj["id"].toString();
        QString label = nodeObj["label"].toString();
        
        QGraphicsEllipseItem* ellipse = m_scene->addEllipse(0, 0, 80, 40, 
                                                          QPen(Qt::black), 
                                                          QBrush(Qt::lightGray));
        QGraphicsTextItem* text = m_scene->addText(label);
        text->setPos(10, 5);
        
        // Simple layout - arrange in grid
        int idx = nodeItems.size();
        int row = idx / 5;
        int col = idx % 5;
        ellipse->setPos(col * 150, row * 100);
        text->setPos(col * 150 + 10, row * 100 + 5);
        
        nodeItems[id] = ellipse;
        m_nodes[id] = ellipse;
    }
    
    // Create edges
    QJsonArray edges = json["edges"].toArray();
    for (const QJsonValue &edge : edges) {
        QJsonObject edgeObj = edge.toObject();
        QString from = edgeObj["from"].toString();
        QString to = edgeObj["to"].toString();
        
        if (nodeItems.contains(from) && nodeItems.contains(to)) {
            QGraphicsEllipseItem* fromItem = nodeItems[from];
            QGraphicsEllipseItem* toItem = nodeItems[to];
            
            QLineF line(fromItem->rect().center() + fromItem->pos(),
                       toItem->rect().center() + toItem->pos());
            QGraphicsLineItem* edgeItem = m_scene->addLine(line, QPen(Qt::black, 1.5));
            edgeItem->setData(0, "edge");  // Mark as edge
            edgeItem->setData(1, from);    // Store source
            edgeItem->setData(2, to);      // Store target
            m_edges.append(qMakePair(from, to)); // Store relationship
        }
    }
    
    fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}

void CustomGraphView::displayMergedGraph(const QJsonObject &mergedGraph) {
    parseJson(QJsonDocument(mergedGraph).toJson());
}

void CustomGraphView::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void CustomGraphView::clear()
{
    // Clear all items
    if (m_scene) {
        m_scene->clear();
    }
    m_nodes.clear();
    m_edges.clear();
    m_nodeLevels.clear();
    
    // Reinitialize basic items
    if (m_scene) {
        auto text = m_scene->addText("Cleared");
        text->setPos(20, 20);
    }
}

CustomGraphView::~CustomGraphView()
{
    // Clear containers first
    m_nodes.clear();
    m_edges.clear();
    m_nodeLevels.clear();
    
    // Delete the scene
    if (m_scene) {
        m_scene->clear();
        
        if (m_scene->parent() == this) {
            delete m_scene;
        }
        m_scene = nullptr;
    }
}