# Third-party dependencies

`cmake/dependencies.lock.json` records the exact source archive revision and SHA-256. `tools/bootstrap.py` verifies archives before extraction and retains licences while excluding unused assets/test trees. After bootstrap the build does not fetch network dependencies. SQLite is currently the system/toolchain library rather than a pinned amalgamation.

| Component | Pin | Licence | Use |
|---|---|---|---|
| SDL3 | release-3.2.8 / `f6864924` | zlib | Native window and input |
| bgfx | `fe9bbdec` | BSD-2-Clause | Renderer and precompiled debug-draw/instancing shaders |
| bx | `0e5bbca6` | BSD-2-Clause | bgfx portability |
| bimg | `9bfbd649` | BSD-2-Clause | bgfx image support |
| bgfx.cmake | `5ae5fa5b` | CC0-1.0 | bgfx CMake integration |
| GLM | 1.0.1 | MIT or Happy Bunny | Vector, quaternion and matrix mathematics |
| FastNoise2 | 1.1.1 / `903c1f2d` | MIT | Strict-FP batched micro-inclusion noise |
| FastSIMD | `16450dae` | MIT | FastNoise2 SIMD dispatch |
| oneTBB | 2022.1.0 | Apache-2.0 | Background generation |
| Zstandard | 1.5.7 | BSD-3-Clause or GPL-2.0 | Sparse payload compression (BSD option) |
| SQLite | system, >=3.35 | Public domain | Sparse modifications and explicit saved state |

Major upstream licences are copied into `third_party_licenses/`. Additional bgfx/bimg bundled libraries retain their notices in `external/bgfx/3rdparty`, `external/bimg/3rdparty` and `external/bx/3rdparty`; those notices must accompany redistribution where required. The build includes bgfx renderer code, bx, bimg and its ASTC support; unused tools are disabled. This project does not relicense third-party source or precompiled shaders.

The precompiled shaders are unmodified files from the pinned bgfx archive. `vs_debugdraw_lines`/`fs_debugdraw_lines` carry vertex colours; `vs_instancing`/`fs_instancing` use a per-instance matrix and colour. The renderer consumes their existing layouts. Native Metal, GLSL, SPIR-V and Direct3D binaries come from the same pinned revision as the renderer. Shader sources and their upstream build tools are available in the [bgfx repository](https://github.com/bkaradzic/bgfx/tree/fe9bbdec66110f8626b9251a4947fc336ac3f62c).

Upstream primary references: [SDL native-window properties](https://wiki.libsdl.org/SDL3/SDL_GetWindowProperties), [bgfx API](https://bkaradzic.github.io/bgfx/bgfx.html), [FastNoise2 compilation and strict-FP](https://github.com/Auburn/FastNoise2/wiki/Compiling-FastNoise2), [SQLite public-domain dedication](https://sqlite.org/copyright.html).
