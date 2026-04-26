# AgriFlow 3D

**Real-Time Agricultural Supply Chain Visualizer**  
Computer Graphics & Visualization (CGV) Project · April 2026

---

## What is AgriFlow 3D?

AgriFlow 3D is an interactive OpenGL application that visualizes an agricultural supply chain across India's farming regions. It renders a 3D landscape with farms, warehouses, cold-storage facilities, and markets connected by road routes. Animated trucks carry cargo between nodes while a dynamic day/night sky cycle runs in the background.

The core idea: press **O** to toggle Dijkstra's shortest-path algorithm and instantly see how route optimization cuts spoilage by ~65% and more than doubles farmer revenue — all visualized live in 3D.

---

## Features

- **10 supply-chain nodes** — 4 Farms, 2 Warehouses, 1 Cold Storage, 3 Markets
- **18 weighted road edges** — cost = distance + spoilage rate + middleman penalty
- **Dijkstra route optimization** — toggle ON/OFF with **O**, optimal path glows green
- **Live spoilage tracking** — truck cargo colour shifts green → yellow → red in real time
- **Detailed truck models** — cab, chassis, cargo box, 6 animated spinning wheels, headlights
- **HDR Bloom** — 16-bit float FBO, bright-pass, 8-pass Gaussian blur, Reinhard tone-map
- **Procedural sky** — FBM clouds (day), twinkling stars + Milky Way band (night)
- **Height-based terrain** — water, sand, farmland, hills, snow peaks with animated water shimmer
- **Pixel-font HUD** — built-in 5×7 bitmap font, stats panel, minimap, control pills
- **Day/Night cycle** — press 1/2/3 for ×1/×5/×20 time speed

---

## Supply Chain Nodes

| ID | Name | Type |
|---|---|---|
| 0 | Vidarbha Wheat Farm | Farm |
| 1 | Nashik Cooperative | Farm |
| 2 | Ludhiana Fields | Farm |
| 3 | Amritsar Farm | Farm |
| 4 | Nagpur Warehouse | Warehouse |
| 5 | Ludhiana Hub | Warehouse |
| 6 | Nashik Cold Chain | Cold Storage |
| 7 | Delhi Main Mandi | Market |
| 8 | Delhi Retail | Market |
| 9 | New Delhi Market | Market |

---

## Build Instructions

### Requirements

| Dependency | Version | Notes |
|---|---|---|
| GCC / MinGW-w64 | 11+ | C++17 required |
| GLFW | 3.3.8 | Bundled in `glfw-3.3.8.bin.WIN32/` |
| GLAD | GL 3.3 | `src/glad.c` + `include/glad/` |
| GLM | 0.9.9+ | Header-only in `include/glm/` |

### Windows (MinGW)

```bash
g++ -std=c++17 -O2 src/main.cpp src/glad.c ^
    -Iinclude -Llib ^
    -lglfw3 -lopengl32 -lgdi32 ^
    -o agriflow.exe

agriflow.exe
```

### CMake (Linux / macOS)

```bash
mkdir build && cd build
cmake ..
make -j4
./AgriFlow3D
```

---

## Controls

| Input | Action |
|---|---|
| **W A S D** | Fly camera |
| **Space / Q** | Fly up |
| **Ctrl / E** | Fly down |
| **Right-click + drag** | Look around |
| **Scroll wheel** | Zoom |
| **O** | Toggle route optimization ON / OFF |
| **Left-click** on node | Select node, re-run Dijkstra |
| **F1** | Overview camera |
| **F2** | Focus Farm 0 (Vidarbha) |
| **F3** | Focus Market 7 (Delhi) |
| **1 / 2 / 3** | Day speed ×1 / ×5 / ×20 |
| **Escape** | Quit |

---

## Results

| Metric | Without Optimization | With Optimization | Improvement |
|---|---|---|---|
| Route distance | 128 km | 80 km | −37.5% |
| Spoilage rate | 76% | 27% | −64.5% |
| Middlemen | 3 | 0 | −100% |
| Farmer revenue | INR 800 | INR 2,100 | +162.5% |
| Fresh deliveries | ~30% trucks | ~85% trucks | +183% |

---

## Technology Stack

- **C++17** — single source file (`src/main.cpp`, ~2,500 lines)
- **OpenGL 3.3 Core Profile** — all rendering
- **GLFW 3.3** — window and input
- **GLAD** — OpenGL function loading
- **GLM** — math (vectors, matrices, transforms)

---

## Project Structure

```
AgriFlow3D/
├── src/
│   ├── main.cpp          # Full application source
│   └── glad.c            # OpenGL loader
├── include/
│   ├── glad/             # GLAD headers
│   ├── GLFW/             # GLFW headers
│   └── glm/              # GLM math library (header-only)
├── lib/
│   └── libglfw3.a        # Pre-built GLFW (WIN32)
├── glfw-3.3.8.bin.WIN32/ # Full GLFW distribution
├── docs/
│   └── AgriFlow3D_Documentation.md  # Full project documentation
├── CMakeLists.txt
└── README.md
```

---

## Documentation

Full documentation (architecture, graphics concepts, algorithm explanation, controls, and results) is available in [`docs/AgriFlow3D_Documentation.md`](docs/AgriFlow3D_Documentation.md).

---

*AgriFlow 3D — Computer Graphics & Visualization Project · 2026*
