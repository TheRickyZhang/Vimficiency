# VimUtils Design Principles

## 1. Validate State Strictly
When calling our underlying VimUtils, we should be using assertions to error on any redundant actions. This is because early pruning is always preferred, so it should be assumed when searching that we will never explore options that are easily checkable (for instance don't search j if we are on the last line). This extends to count validation of deterministic outcomes.

For instance, "j" should never be explored if on the last line, and neither should 3dd on the second to last line.

## 2. Handle Empty Representation
Since we must distinguish one empty line/column, and no columns, we thus must handle emptiness explicitly.

To put it more explicitly:
- `line < lines.size()` EXCEPT when lines.empty(), in which case line=0, col=0.
- `col < lines[line].size()` EXCEPT when `lines[line].empty()`, in which case col=0.

## 3. Minimal API
Single-line operations only need the context of the line.
