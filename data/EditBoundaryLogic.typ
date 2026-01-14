= EditBoundary Crossing Logic

== Notation

For forward motions (dw, de, dW, dE):
- `sss` = chars to delete (edit region)
- `|` = boundary
- `nnn` = chars outside (must not touch)
- lastChar = last char of edit region (rightmost 's')
- boundaryChar = first char after boundary (leftmost 'n')

For backward motions (db, dge, dB, dgE):
- `nnn` = chars outside (must not touch)
- `|` = boundary
- `sss` = chars to delete (edit region)
- firstChar = first char of edit region (leftmost 's')
- boundaryChar = last char before boundary (rightmost 'n')

CharTypes: e = keyword (alphanumeric/_), ` ` = whitespace, `.` = symbol/punctuation

== Forward Motions

=== dw (delete to next word start, includes trailing whitespace)

```
              |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
--------------+-------------------+-------------------+-------------------+
last=keyword  |  YES  `sse|ann`   |  YES  `sse| nn`   |  no   `sse|.nn`   |
last=space    |  no   `ss |ann`   |  YES  `ss | nn`   |  no   `ss |.nn`   |
last=symbol   |  no   `ss.|ann`   |  no   `ss.| nn`   |  no   `ss.|.nn`   |
```

=== de (delete to word end)

```
              |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
--------------+-------------------+-------------------+-------------------+
last=keyword  |  YES  `sse|ann`   |  no   `sse| nn`   |  no   `sse|.nn`   |
last=space    |  no   `ss |ann`   |  no   `ss | nn`   |  no   `ss |.nn`   |
last=symbol   |  no   `ss.|ann`   |  no   `ss.| nn`   |  no   `ss.|.nn`   |
```

=== dW (delete to next WORD start, includes trailing whitespace)

```
              |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
--------------+-------------------+-------------------+-------------------+
last=keyword  |  YES  `sse|ann`   |  YES  `sse| nn`   |  YES  `sse|.nn`   |
last=space    |  no   `ss |ann`   |  YES  `ss | nn`   |  no   `ss |.nn`   |
last=symbol   |  YES  `ss.|ann`   |  YES  `ss.| nn`   |  YES  `ss.|.nn`   |
```

=== dE (delete to WORD end)

```
              |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
--------------+-------------------+-------------------+-------------------+
last=keyword  |  YES  `sse|ann`   |  no   `sse| nn`   |  YES  `sse|.nn`   |
last=space    |  no   `ss |ann`   |  no   `ss | nn`   |  no   `ss |.nn`   |
last=symbol   |  YES  `ss.|ann`   |  no   `ss.| nn`   |  YES  `ss.|.nn`   |
```

== Backward Motions

=== db (delete backward to word start)

```
               |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
---------------+-------------------+-------------------+-------------------+
first=keyword  |  YES  `nna|ess`   |  no   `nn |ess`   |  no   `nn.|ess`   |
first=space    |  no   `nna| ss`   |  no   `nn | ss`   |  no   `nn.| ss`   |
first=symbol   |  no   `nna|.ss`   |  no   `nn |.ss`   |  no   `nn.|.ss`   |
```

=== dge (delete backward to previous word end)

Note: dge must find the END of the PREVIOUS word, so it always goes PAST the current position unless firstChar is already at a word end (symbol).

```
               |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
---------------+-------------------+-------------------+-------------------+
first=keyword  |  YES  `nna|ess`   |  YES  `nn |ess`   |  YES  `nn.|ess`   |
first=space    |  YES  `nna| ss`   |  YES  `nn | ss`   |  YES  `nn.| ss`   |
first=symbol   |  no   `nna|.ss`   |  no   `nn |.ss`   |  no   `nn.|.ss`   |
```

=== dB (delete backward to WORD start)

```
               |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
---------------+-------------------+-------------------+-------------------+
first=keyword  |  YES  `nna|ess`   |  no   `nn |ess`   |  YES  `nn.|ess`   |
first=space    |  no   `nna| ss`   |  no   `nn | ss`   |  no   `nn.| ss`   |
first=symbol   |  YES  `nna|.ss`   |  no   `nn |.ss`   |  YES  `nn.|.ss`   |
```

=== dgE (delete backward to previous WORD end)

```
               |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
---------------+-------------------+-------------------+-------------------+
first=keyword  |  YES  `nna|ess`   |  YES  `nn |ess`   |  YES  `nn.|ess`   |
first=space    |  YES  `nna| ss`   |  YES  `nn | ss`   |  YES  `nn.| ss`   |
first=symbol   |  YES  `nna|.ss`   |  no   `nn |.ss`   |  YES  `nn.|.ss`   |
```

== Symmetry Analysis

Pairs with similar behavior:
- **(de, db)**: Both "conservative" - only cross when same word-class on both sides
- **(dw, dge)**: Both "aggressive" - dw includes trailing space, dge must find previous word end

But NOT perfectly symmetric:
- dw depends on both (lastChar, boundaryChar)
- dge depends mainly on firstChar (safe only when Symbol)

The asymmetry is fundamental:
- dw "to next word START" = delete current + trailing space → reaches INTO next region
- dge "to prev word END" = must GO BACK from current position → goes OUTSIDE current region

== Summary Tables

=== Forward: safe when canCross returns false

```
canDwCross(last, bc):  last=Symbol → false; last=Keyword → bc!=Symbol; last=Space → bc==Space
canDeCross(last, bc):  last==Keyword && bc==Keyword
canDWCross(last, bc):  last!=Space && (bc!=Space || last==Space&&bc==Space)  // simplified: last==Space → bc==Space; else true except bc==Newline
canDECross(last, bc):  last!=Space && bc!=Space
```

=== Backward: safe when canCross returns false

```
canDbCross(first, bc):   first==Keyword && bc==Keyword
canDgeCross(first, bc):  first!=Symbol
canDBCross(first, bc):   first!=Space && bc!=Space
canDgECross(first, bc):  first!=Space && (first!=Symbol || bc==Symbol)
```

== Newline Handling

When boundaryChar == Newline (at line end/start), motion cannot cross (nothing to cross into).
All canXxxCross functions return false when bc==Newline.

== Implementation Notes

```cpp
enum class CharType : uint8_t { Keyword, Whitespace, Symbol, Newline };

// Forward
bool canDwCross(CharType last, CharType bc) {
    if (bc == Newline) return false;
    if (last == Symbol) return false;
    if (last == Keyword) return bc != Symbol;
    return bc == Whitespace; // last == Whitespace
}

bool canDeCross(CharType last, CharType bc) {
    if (bc == Newline) return false;
    return last == Keyword && bc == Keyword;
}

bool canDWCross(CharType last, CharType bc) {
    if (bc == Newline) return false;
    if (last == Whitespace) return bc == Whitespace;
    return true; // Keyword or Symbol: always crosses
}

bool canDECross(CharType last, CharType bc) {
    if (bc == Newline) return false;
    if (last == Whitespace) return false;
    return bc != Whitespace; // Keyword/Symbol cross to Keyword/Symbol
}

// Backward
bool canDbCross(CharType first, CharType bc) {
    if (bc == Newline) return false;
    return first == Keyword && bc == Keyword;
}

bool canDgeCross(CharType first, CharType bc) {
    if (bc == Newline) return false;
    return first != Symbol;
}

bool canDBCross(CharType first, CharType bc) {
    if (bc == Newline) return false;
    if (first == Whitespace) return false;
    return bc != Whitespace;
}

bool canDgECross(CharType first, CharType bc) {
    if (bc == Newline) return false;
    if (first == Whitespace) return false;
    if (first == Symbol) return bc == Symbol;
    return true; // first == Keyword: always crosses
}
```
