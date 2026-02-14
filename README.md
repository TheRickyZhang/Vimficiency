# Vimficiency

One of the biggest challenges people face with learning Vim motions is the high learning curve. It is often very difficult to know which of the many ways to perform the same action is the most efficient, or even what all the applicable actions are.

Vimficiency bridges this gap by analyzing your editing actions and suggesting more efficient key sequences, with best effort awareness of possible actions and customizability. It is genuinely a difficult algorithmic and heuristical problem!


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

### Manual triggerss
```vim
:Vimfy start a      " Start recording session 'a'
" ... edit normally ...
:Vimfy end a        " Finish and show optimization suggestions

" ... edit normally ...
:Vimfy end @        " Finish from past session

" ... edit normally ...
:Vimfy end @        " Finish from past session
```

## License

MIT

## Docs
For more specific information, see /docs.
