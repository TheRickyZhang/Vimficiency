// Dev fixture — sample exploration data for local development
window.EXPLORATION_DATA = {
  lastUpdate: Date.now(),
  entries: [
    {
      commit: { id: "abc1234567890", message: "Initial exploration", timestamp: "2025-01-02T00:00:00Z", url: "#", author: { username: "dev" } },
      date: 1704153600000,
      cases: [
        {
          name: "BufferSize/5",
          nodesExplored: 106,
          states: [
            { effort: 0.0, seq: "" },
            { effort: 1.0, seq: "j" },
            { effort: 1.0, seq: "k" },
            { effort: 1.0, seq: "l" },
            { effort: 1.0, seq: "h" },
            { effort: 1.2, seq: "w" },
            { effort: 1.2, seq: "b" },
            { effort: 1.5, seq: "e" },
            { effort: 2.0, seq: "jj" },
            { effort: 2.0, seq: "jl" },
            { effort: 2.1, seq: "jw" },
            { effort: 2.2, seq: "kk" },
            { effort: 2.3, seq: "ww" },
            { effort: 2.5, seq: "3j" },
            { effort: 2.8, seq: "jjj" },
            { effort: 3.0, seq: "3w" },
            { effort: 3.2, seq: "jjl" },
            { effort: 3.5, seq: "4j" },
            { effort: 3.8, seq: "jjjj" },
            { effort: 4.0, seq: "3wl" },
            { effort: 4.2, seq: "jjjjl" },
            { effort: 4.5, seq: "5j" },
            { effort: 4.8, seq: "jjjjjj" },
            { effort: 5.0, seq: "Gk" },
            { effort: 5.5, seq: "G" }
          ]
        },
        {
          name: "LineLength/40",
          nodesExplored: 85,
          states: [
            { effort: 0.0, seq: "" },
            { effort: 1.0, seq: "l" },
            { effort: 1.0, seq: "h" },
            { effort: 1.2, seq: "w" },
            { effort: 1.2, seq: "b" },
            { effort: 1.5, seq: "e" },
            { effort: 2.0, seq: "ll" },
            { effort: 2.0, seq: "wl" },
            { effort: 2.2, seq: "ww" },
            { effort: 2.5, seq: "2w" },
            { effort: 2.8, seq: "3w" },
            { effort: 3.0, seq: "fl" },
            { effort: 3.2, seq: "fa" },
            { effort: 3.5, seq: "$" },
            { effort: 4.0, seq: "0" },
            { effort: 4.2, seq: "^" },
            { effort: 4.5, seq: "4w" },
            { effort: 5.0, seq: "f;w" },
            { effort: 5.5, seq: "2f;" },
            { effort: 6.0, seq: "$b" }
          ]
        },
        {
          name: "BufferSize/10",
          nodesExplored: 200,
          states: Array.from({ length: 80 }, (_, i) => ({
            effort: i * 0.15,
            seq: ["j", "k", "l", "h", "w", "b", "e", "0", "$", "G"][i % 10] + (i > 10 ? "j".repeat(Math.floor(i / 10)) : "")
          }))
        }
      ]
    },
    {
      commit: { id: "def4567890123", message: "Optimize search", timestamp: "2025-01-03T00:00:00Z", url: "#", author: { username: "dev" } },
      date: 1704240000000,
      cases: [
        {
          name: "BufferSize/5",
          nodesExplored: 95,
          states: [
            { effort: 0.0, seq: "" },
            { effort: 1.0, seq: "j" },
            { effort: 1.0, seq: "l" },
            { effort: 1.2, seq: "w" },
            { effort: 2.0, seq: "jj" },
            { effort: 2.1, seq: "jw" },
            { effort: 2.5, seq: "3j" },
            { effort: 3.0, seq: "3w" },
            { effort: 3.5, seq: "4j" },
            { effort: 4.0, seq: "3wl" },
            { effort: 4.5, seq: "5j" },
            { effort: 5.0, seq: "Gk" },
            { effort: 5.5, seq: "G" }
          ]
        },
        {
          name: "LineLength/40",
          nodesExplored: 70,
          states: [
            { effort: 0.0, seq: "" },
            { effort: 1.0, seq: "l" },
            { effort: 1.2, seq: "w" },
            { effort: 2.0, seq: "wl" },
            { effort: 2.2, seq: "ww" },
            { effort: 2.5, seq: "2w" },
            { effort: 3.0, seq: "fl" },
            { effort: 3.5, seq: "$" },
            { effort: 4.0, seq: "0" },
            { effort: 4.5, seq: "4w" },
            { effort: 5.0, seq: "f;w" }
          ]
        },
        {
          name: "BufferSize/10",
          nodesExplored: 180,
          states: Array.from({ length: 70 }, (_, i) => ({
            effort: i * 0.16,
            seq: ["j", "k", "l", "h", "w", "b", "e", "0", "$", "G"][i % 10] + (i > 10 ? "j".repeat(Math.floor(i / 10)) : "")
          }))
        }
      ]
    }
  ]
};
