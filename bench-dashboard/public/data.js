// Dev fixture — sample benchmark data for local development
window.BENCHMARK_DATA = {
  lastUpdate: Date.now(),
  repoUrl: "https://github.com/example/vimficiency",
  entries: {
    "Benchmark": [
      {
        commit: { id: "abc1234567890", message: "Initial benchmarks", timestamp: "2025-01-01T00:00:00Z", url: "#", author: { username: "dev" } },
        date: 1704067200000,
        benches: [
          { name: "EditOptimizer/Replace/SingleChar", value: 45000, unit: "ns" },
          { name: "EditOptimizer/Replace/MultiChar", value: 120000, unit: "ns" },
          { name: "EditOptimizer/Change/Word", value: 350000, unit: "ns" },
          { name: "EditOptimizer/Change/Line", value: 280000, unit: "ns" },
          { name: "EditOptimizer/Insert/Beginning", value: 95000, unit: "ns" },
          { name: "EditOptimizer/Insert/End", value: 88000, unit: "ns" },
        ]
      },
      {
        commit: { id: "def4567890123", message: "Optimize replace path", timestamp: "2025-01-02T00:00:00Z", url: "#", author: { username: "dev" } },
        date: 1704153600000,
        benches: [
          { name: "EditOptimizer/Replace/SingleChar", value: 42000, unit: "ns" },
          { name: "EditOptimizer/Replace/MultiChar", value: 115000, unit: "ns" },
          { name: "EditOptimizer/Change/Word", value: 340000, unit: "ns" },
          { name: "EditOptimizer/Change/Line", value: 275000, unit: "ns" },
          { name: "EditOptimizer/Insert/Beginning", value: 93000, unit: "ns" },
          { name: "EditOptimizer/Insert/End", value: 86000, unit: "ns" },
        ]
      },
      {
        commit: { id: "ghi7890123456", message: "Add caching layer", timestamp: "2025-01-03T00:00:00Z", url: "#", author: { username: "dev" } },
        date: 1704240000000,
        benches: [
          { name: "EditOptimizer/Replace/SingleChar", value: 38000, unit: "ns" },
          { name: "EditOptimizer/Replace/MultiChar", value: 105000, unit: "ns" },
          { name: "EditOptimizer/Change/Word", value: 320000, unit: "ns" },
          { name: "EditOptimizer/Change/Line", value: 260000, unit: "ns" },
          { name: "EditOptimizer/Insert/Beginning", value: 90000, unit: "ns" },
          { name: "EditOptimizer/Insert/End", value: 82000, unit: "ns" },
        ]
      }
    ]
  }
};
