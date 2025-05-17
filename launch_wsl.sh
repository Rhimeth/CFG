#!/bin/bash
# Force software rendering
export QT_XCB_FORCE_SOFTWARE_OPENGL=1
export LIBGL_ALWAYS_SOFTWARE=1
export QT_QUICK_BACKEND=software

# Limit memory usage
export CLANG_MEMORY_LIMIT=2048

# Run with reduced thread count
taskset -c 0 ./CFGParser "$@"