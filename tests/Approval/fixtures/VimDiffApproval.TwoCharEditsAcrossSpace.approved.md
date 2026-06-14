# two char edits across a space

**initial**
```
abc xyz
```

**final**
```
aXc xYz
```

_Edit costs: delete / insert / move_

### Plan 1: cost 8

```
b
->
X
```
`1 / 1 / 0`

```
y
->
Y
```
`1 / 1 / 2`

### Plan 2: cost 9

```
b
->
X
```
`1 / 1 / 0`

```
xy
->
xY
```
`2 / 2 / 1`

### Plan 3: cost 9

```
bc
->
Xc
```
`2 / 2 / 0`

```
y
->
Y
```
`1 / 1 / 1`
