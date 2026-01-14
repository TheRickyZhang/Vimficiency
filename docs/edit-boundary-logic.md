= EditBoundary Crossing Logic

== Endpoint Types (Core Abstraction)

All deletion operations find an *endpoint* - where deletion stops. Three types:
End:   Last char of word
Space: Char before next word
Next:  Start of next word


A deletion operation from currCol to endpoint is INCLUSIVE.

Direction determines which boundary is "end":
- Forward: end = rightmost char
- Backward: end = leftmost char

== Crossing Tables

Each endpoint type has a crossing table. Motion is *safe* when table returns `no`.

=== End
```
              |  bc=Keyword  |  bc=Whitespace  |  bc=Symbol  |  bc=Newline  |
--------------+--------------+-----------------+-------------+--------------+
char=Keyword  |  YES         |  no             |  no         |  no          |
char=Space    |  YES         |  YES            |  YES        |  no          |
char=Symbol   |  no          |  no             |  YES        |  no          |
```
Note: Whitespace isn't a word, so `e` from whitespace goes to NEXT word end, crossing everything.
Symbol row mirrors Keyword row (wordChar/nonWordChar symmetry).

=== Space
```
              |  bc=Keyword  |  bc=Whitespace  |  bc=Symbol  |  bc=Newline  |
--------------+--------------+-----------------+-------------+--------------+
char=Keyword  |  YES         |  YES            |  no         |  no          |
char=Space    |  no          |  YES            |  no         |  no          |
char=Symbol   |  no          |  YES            |  YES        |  no          |
```
Note: Symbol row mirrors Keyword row (wordChar/nonWordChar symmetry).

=== Next
```
              |  bc=Keyword  |  bc=Whitespace  |  bc=Symbol  |  bc=Newline  |
--------------+--------------+-----------------+-------------+--------------+
char=Keyword  |  YES         |  YES            |  YES        |  no          |
char=Space    |  YES         |  YES            |  YES        |  no          |
char=Symbol   |  YES         |  YES            |  YES        |  no          |
```
Note: `ge` always goes to previous word end, regardless of current content type.

=== Line
```
              |  bc=Keyword  |  bc=Whitespace  |  bc=Symbol  |  bc=Newline  |
--------------+--------------+-----------------+-------------+--------------+
              |  YES         |  YES            |  YES        |  no          |
```


== Applying Crossing Checks
- *Forward*: check `(lastChar, rightBoundary)` using the endpoint's table
- *Backward*: check `(firstChar, leftBoundary)` using the endpoint's table

Where:
- `lastChar` / `firstChar` = char at the edge of current content
- `rightBoundary` / `leftBoundary` = char just OUTSIDE the edit region

== Vim Deletion Commands
Here, you can see that de/db are symmetric, and vim only gives us a few combinations compared to all possibilities.
```
Command |
de   Current Char + (Forward, End) from next char
db   Current Char + (Backward  End) from next char
dw   (Forward, Space)
dge  (Backward, Next)
```

== Text Object Commands
```
Command |
diw  (Backward, End) + (Forward, End)
daw  {
  Cursor in word/sentence word:
    Has trailing whitespace/newline: (Backward, End) + (Forward, Space)
    Else: (Backward, Space) + (Forward, End)
  Cursor in whitespace: 
    (Backward, Space) + (Forward, End)
}
```

== WORD Variants
Same endpoint types, but Keyword and Symbol merge into "NonWS" class.

=== END
```
              |  bc=NonWS  |  bc=Whitespace  |  bc=Newline  |
--------------+------------+-----------------+--------------+
char=WORD     |  YES       |  no             |  no          |
char=Space    |  YES       |  YES            |  no          |
```
Note: Whitespace isn't a WORD, so `E` from whitespace goes to NEXT WORD end.

=== SPACE
```
              |  bc=NonWS  |  bc=Whitespace  |  bc=Newline  |
--------------+------------+-----------------+--------------+
char=WORD     |  YES       |  YES            |  no          |
char=Space    |  no        |  YES            |  no          |
```

=== NEXT
```
              |  bc=NonWS  |  bc=Whitespace  |  bc=Newline  |
--------------+------------+-----------------+--------------+
char=WORD     |  YES       |  YES            |  no          |
char=Space    |  YES       |  YES            |  no          |
```


== Vim Deletion Commands
Here, you can see that de/db are symmetric, and vim only gives us a few combinations compared to all possibilities.
```
Command |
dE   Current Char + (Forward, END) from next char
dB   Current Char + (Backward, END) from next char
dW   (Forward, SPACE)
dgE  (Backward, NEXT)
```

== Text Object Commands
Command
diW  (Backward, END) + (Forward, END)
daW  {
  Cursor in word/sentence word:
    Has trailing whitespace/newline: (Backward, END) + (Forward, SPACE)
    Else: (Backward, SPACE) + (Forward, END)
  Cursor in whitespace: 
    (Backward, SPACE) + (Forward, END)
}

== Other commands (TODO in future)
```
dd   (Line)
dip, dap
dib, dab
```


.n l.n :Qaejp 
e @i:bmi : 










