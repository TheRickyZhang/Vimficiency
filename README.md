# Vimficiency

One of the biggest challenges people face with learning Vim motions is the high learning curve. It is often very difficult to know which of the many ways to perform the same action is the most efficient, or even what all the applicable actions are.

Vimficiency bridges this gap by analyzing your editing actions and suggesting more efficient key sequences, with best effort awareness of possible actions and customizability. It is genuinely a difficult algorithmic and heuristical problem!

The benchmark site providing more details about the search process is available here:
https://therickyzhang.github.io/Vimficiency/

## Requirements
- Neovim 0.11+
- CMake 4.1+
- C++23

## Build
```bash
cmake -B build
cmake --build build -j
```

## Installation
Add to your Neovim config:

```lua
require('vimficiency').setup()
```

Ensure `build/libvimficiency.so` is in your library path or set `VIMFICIENCY_LIB_PATH`.

## Usage

### Manual triggers
```vim
:Vimfy start a      " Start recording session 'a'
" ... edit normally ...
:Vimfy end a        " Finish and show optimization suggestions

" Or, retrospectively, without starting anything:
" ... edit normally ...
:Vimfy recall 6     " Analyze the last 6 keystrokes
:Vimfy recall 3s    " Analyze the last 3 seconds
```

## License

MIT

## Docs
For more specific information, see /docs.
