# Directory Structure

> How backend code is organized in this project.

---

## Overview

This project uses a **three-tier architecture** for C++ modules:
- `api/` - Public API layer (interface exposed to other modules)
- `source/` - Implementation layer (internal logic and data structures)
- `test/` - Unit tests (mirrors source structure)

Each module is self-contained with its own CMakeLists.txt and follows consistent organization patterns.

---

## Directory Layout

### iCTS Module Structure (Reference Example)

```
src/operation/iCTS/
├── api/                          # Public API layer
│   ├── CTSAPI.hh                # Main API interface
│   └── CTSAPI.cc
├── source/                       # Implementation layer
│   ├── database/                # Data models and I/O
│   │   ├── config/              # Configuration singleton
│   │   ├── design/              # Core data classes (Clock, Inst, Net, Pin)
│   │   ├── io/                  # DB wrapper (iDB integration)
│   │   ├── spatial/             # Spatial data structures (Point, Tree)
│   │   └── characterization/    # Pattern characterization data
│   ├── module/                  # Algorithm modules
│   │   ├── topology/            # Topology generation (clustering, kmeans, mcf)
│   │   ├── routing/             # Routing algorithms (FLUTE, SALT, BST)
│   │   ├── characterization/    # Pattern characterization
│   │   ├── buffering/           # Buffer insertion
│   │   ├── drv/                 # Design rule violation fixing
│   │   ├── optimization/        # Optimization passes
│   │   └── report/              # Reporting
│   ├── utils/                   # Utilities
│   │   ├── logger/              # Module-specific logger
│   │   └── geometry/            # Geometry helpers
│   └── flow/                    # Flow management
├── test/                        # Unit tests (mirrors source structure)
│   ├── common/                  # Test utilities
│   ├── database/
│   ├── module/
│   └── main.cc
├── external_libs/               # External library CMake configs
└── CMakeLists.txt
```

---

## Module Organization

### 1. API Layer (`api/`)

**Purpose**: Public interface exposed to other modules

**Contents**:
- Main API class (e.g., `CTSAPI`)
- Public header files only
- Minimal dependencies

**Example**: `src/operation/iCTS/api/CTSAPI.hh`

### 2. Source Layer (`source/`)

**Purpose**: Internal implementation (not exposed to other modules)

**Subdirectories**:
- `database/` - Data models, configuration, I/O wrappers
- `module/` - Algorithm implementations
- `utils/` - Module-specific utilities (logger, geometry, etc.)
- `flow/` - Flow management and orchestration

### 3. Test Layer (`test/`)

**Purpose**: Unit tests

**Organization**: Mirrors `source/` structure
- `test/database/` tests `source/database/`
- `test/module/` tests `source/module/`
- `test/common/` contains shared test utilities

---

## Naming Conventions

### Files

- **Headers**: `.hh` extension (not `.h`)
- **Source**: `.cc` extension (not `.cpp`)
- **Naming**: PascalCase (e.g., `TopologyGen.hh`, `Clustering.cc`)

### Directories

- **Lowercase with underscores** for multi-word names
- Examples: `database/`, `module/`, `external_libs/`

### CMake Organization

Each subdirectory has its own `CMakeLists.txt`:

```cmake
# Interface libraries aggregate components
add_library(icts_source_database INTERFACE)
add_library(icts_source_module INTERFACE)

# Debug options per component
option(DEBUG_ICTS_SOURCE_DATABASE "Enable debug for iCTS database" OFF)
option(DEBUG_ICTS_SOURCE_MODULE "Enable debug for iCTS module" OFF)
```

---

## Examples

### Well-Organized Modules

1. **iCTS** (`src/operation/iCTS/`) - Clock Tree Synthesis
   - Clean three-tier architecture
   - Clear separation of concerns
   - Comprehensive test coverage

2. **Database Layer** (`src/operation/iCTS/source/database/`)
   - `config/` - Configuration management
   - `design/` - Core data classes
   - `io/` - External integration
   - `spatial/` - Spatial data structures

3. **Module Layer** (`src/operation/iCTS/source/module/`)
   - Each algorithm in its own subdirectory
   - Self-contained implementations
   - Clear dependencies

---

## Key Principles

1. **Separation of Concerns**: API, implementation, and tests are clearly separated
2. **Self-Contained Modules**: Each module manages its own dependencies via CMake
3. **Consistent Structure**: All modules follow the same three-tier pattern
4. **Test Mirroring**: Test structure mirrors source structure for easy navigation
