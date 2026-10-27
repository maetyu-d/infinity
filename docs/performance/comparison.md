| Settled view | CPU median before → after | CPU P95 before → after | Submitted detail before → after |
|---|---:|---:|---|
| GALAXY | 5.752 → 3.295 ms | 6.216 → 3.881 ms | 19,606 → 33,606 particles |
| STAR SYSTEM | 0.608 → 0.447 ms | 1.052 → 0.811 ms | 1,408 → 2,808 particles |
| PLANET | 0.520 → 0.246 ms | 0.810 → 0.357 ms | 23,424 → 84,480 triangles |
| SURFACE | 0.549 → 0.257 ms | 0.815 → 0.404 ms | 37,844 → 78,818 triangles |
| MOUNTAIN | 1.372 → 0.407 ms | 1.883 → 0.665 ms | 73,106 → 161,184 triangles |
| ROCK | 2.600 → 0.700 ms | 3.386 → 1.003 ms | 30,052 → 68,564 triangles |
| MINERAL GRAIN | 0.413 → 0.251 ms | 0.652 → 0.379 ms | 10,734 → 26,130 triangles |
| CRYSTAL LATTICE | 0.369 → 0.254 ms | 0.678 → 0.377 ms | 1,026 → 1,395 atoms |

Median peak RSS: 128.4 → 133.5 MiB (+4.0%).
Whole-tour CPU submission P95: 5.772 → 3.280 ms.

| Generation workload | Median before → after | Speedup |
|---|---:|---:|
| GALAXY, requested detail 4 | 17.990 → 30.297 ms | 0.59× |
| STAR SYSTEM, requested detail 0 | 1.137 → 1.394 ms | 0.82× |
| PLANET, requested detail 2 | 21.383 → 5.456 ms | 3.92× |
| SURFACE, requested detail 4 | 84.872 → 29.267 ms | 2.90× |
| MOUNTAIN, requested detail 4 | 84.871 → 29.169 ms | 2.91× |
| ROCK, requested detail 2 | 50.648 → 9.990 ms | 5.07× |
| MINERAL GRAIN, requested detail 2 | 30.290 → 3.971 ms | 7.63× |
| CRYSTAL LATTICE, requested detail 0 | 1.754 → 1.800 ms | 0.97× |
