#include "edge.h"
#include <cmath>

Edge::Edge() : sourceNode(""), destinationNode(""), 
               sourcePosition(0,0), destinationPosition(0,0) {}

Edge::Edge(const QString& source, const QString& destination)
    : sourceNode(source), destinationNode(destination), 
      sourcePosition(0,0), destinationPosition(0,0) {}

Edge::Edge(const QPointF& sourcePos, const QPointF& destPos)
    : sourceNode(""), destinationNode(""), 
      sourcePosition(sourcePos), destinationPosition(destPos) {}

QString Edge::getSourceNode() const {
    return sourceNode;
}

QString Edge::getDestinationNode() const {
    return destinationNode;
}

QPointF Edge::getSourcePosition() const {
    return sourcePosition;
}

QPointF Edge::getDestinationPosition() const {
    return destinationPosition;
}

void Edge::setSourceNode(const QString& source) {
    sourceNode = source;
}

void Edge::setDestinationNode(const QString& destination) {
    destinationNode = destination;
}

void Edge::setSourcePosition(const QPointF& pos) {
    sourcePosition = pos;
}

void Edge::setDestinationPosition(const QPointF& pos) {
    destinationPosition = pos;
}

double Edge::getLength() const {
    return std::sqrt(std::pow(destinationPosition.x() - sourcePosition.x(), 2) +
                     std::pow(destinationPosition.y() - sourcePosition.y(), 2));
}

bool Edge::isValid() const {
    // Consider an edge valid if either node names are set or positions are different
    return !sourceNode.isEmpty() && !destinationNode.isEmpty() ||
           sourcePosition != destinationPosition;
}