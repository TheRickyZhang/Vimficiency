For many useful features, like ability to replay/store calculated optimizations for a snapshot, and smooth integration with workflow, it is useful to know:
- When are calls to start/end invoked?
- How can we keep track of distinct instance?

Three main ways that optimizers can be invoked. Each will store up to N = 5 in memory, and additional will evict in FIFO order.

If any sessions go longer than MAX_SESSION_MOTION_LENGTH, they should be automatically terminated with a warning.

= Manual (5):
Alias = Whatever the user decides (or limit to a b c d e for simplicity). Use a as example
The user invokes "VimficiencyStart a" and "VimficiencyEnd a" to set explicit start/end points. So, it is pretty easy to handle state and keep in memory, and map a -> underlying state id.

= Time (5):
Alias = ., .., ... (#.)
User can set INVOCATION_WAIT_TIME in the config. We monitor their keystrokes, and as soon as INVOCATION_WAIT_TIME passes without any actions, end the ongoing session.
So we guarantee that there is only one time session occurring at once. We keep the 5 most recent sessions in memory; the user can refer to them with number of dots that are going. This relies on accurate detection of the correct type of actions (similar to manual key detection, doing :VimficiencyEnd . should not be updating any state except once executed)

= Key Count Back (10):
Alias = 1, 2, 3, 4, 5
User can set INVOCATION_MAX_KEYS_TYPED = 10 in the config. We maintain sessions that called begin() everything from 1 to INVOCATION_MAX_KEYS_TYPED keys typed ago. When the user refers to 3, it uses id of session from 3 ago, and does optimization with current state.

Note that keys_typed != semantic actions, as gg = 2 keys typed, 1 action, but it is the best solution without maintaining parsing state.

## Output Format

When a session completes, results are displayed with:

1. **Position info**: `(start_row, start_col) -> (end_row, end_col)` (0-indexed)
2. **User sequence**: What the user typed, formatted with spaces between logical units, with effort cost
3. **Optimal results**: Up to N best sequences found, sorted by cost (ascending)

Example output:
```
vimficiency finished [a] (0,0) -> (2,2)
  user: cw xxx <Esc> jj ci yyy <Esc> (15.00)
  1. 3rx <C-d> ciw yyy <Esc> (8.00)
  2. 3rx w <C-d> 3ry (11.00)
  3. 3rx Wlj ciw yyy <Esc> (13.00)
```

### Sequence Formatting

Sequences are tokenized into logical units using `SequenceParser`:
- Motions: `w`, `3j`, `fa;`, `<C-d>`
- Edit commands: `ciw`, `dd`, `D`, `3rx`
- Typed text: Characters typed after entering insert mode
- Special keys: `<Esc>`

Raw sequence: `3rx<C-d>ciwfoo<Esc>` → Formatted: `3rx <C-d> ciw foo <Esc>`

### Cost Calculation

User cost is calculated using `getEffort(keyseq, config)` from `RunningEffort.h`, which:
- Converts the sequence to physical key presses
- Applies keyboard effort model (finger travel, hand alternation, etc.)
- Returns total effort score
