# one-char gap between two edits

**initial**
```
abcde
```

**final**
```
aXcYe
```

_Edit costs: delete / insert / move_

### Plan 1: cost 6

```
bcd
->
XcY
```
`2 / 3 / 0`

### Plan 2: cost 7

```
abcd
->
aXcY
```
`2 / 4 / 0`

### Plan 3: cost 7

```
b
->
X
```
`1 / 1 / 0`

```
d
->
Y
```
`1 / 1 / 1`
