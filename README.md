# CFGParser

CFGParser is a Qt-based GUI tool and library for extracting and visualizing control-flow graphs (CFGs) and function-call dependencies from C/C++ source using Clang/LLVM. It parses source files, builds per-function CFGs (DOT), and provides an interactive visualization (Qt WebEngine/QGraphics) to explore nodes, edges, and source mapping.

## Key features
- Extract per-function CFGs using Clang's CFG API
- Produce DOT output and PNG/SVG visualizations
- Interactive GUI with code highlighting and progressive visualization
- Multi-file analysis producing combined function-dependency graphs
- Exportable DOT/PNG visualizations; supports Graphviz `dot` processing
- Uses Qt WebEngine to render interactive graph views

## Stack
- Language: C++
- Framework / runtime: Qt 5 (Qt Widgets + Qt WebEngine)
- Notable libraries:
  - LLVM / Clang (used for parsing and CFG construction)
  - Qt5 (Core, Gui, Widgets, WebEngine, WebChannel)
  - nlohmann/json (JSON output)
  - Graphviz (optional, `dot` for PNG/SVG generation)

## Repository layout
