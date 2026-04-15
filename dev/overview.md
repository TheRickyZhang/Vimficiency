Fundamentally, Vimficiency is modeled like:

Track key presses and buffer state (Lua)
->
FFI Bridge to call C++ library
->
Optimization engine computes optimal command sequence
-> Return result to be stored in Neovim / file memory context (Lua)

The optimization engine is designed to be customizable to return what is optimal based on your specific keyboard layout / typing effort and user mappings.

There are many difficulties with simulating commands:
- Efficiency: the effective branching factor for searching all viable commands at any given time is around 50-200, not including count and character-targeting modifiers, which is more than chess while state storage and exploration are more expensive as well.
- Composability: the {count}{operator}{count}{motion} model means there are many parameters you can compose in a single action; there is no way to explore a count or operator independently.
- No perfect heuristic: if we use the intuitive closeness in position / buffer contents as progress, often times the best motions may not approach uniformly or intuitively.
- Vim motions are full of edge cases, lacking concrete abstraction even within its original implementation. For instance, daw has many conditions over which "side" of white space it deletes.

Our implementation uses A* search to balance correctness with efficiency, with a reasonable heuristic of key typing cost plus distance to goal. We represent a minimal buffer setup with Lines (vector<string>) and mode. Benchmark tests indicate we can reasonably explore 50000 states, covering ~80 lines for movement, and ~8 lines for edit.

Some paricularly challenging aspects to implement are:\
abstraction over motion types, particularly word. We create different edge types.
creating a boundary to not do actions outside a current subbuffer (pass only subset for efficiency)
composing multiple edit to be consistent and reasonable in producing an answer.

Lua uses an FFI bridge to call the C++ library for efficiency. 

Tests are very crucial to respecting the edge cases of vim. We primarily use Neovim itself with the Neovim oracle as the source of truth for how actions should execute, and any deviation is a correctness issue.


