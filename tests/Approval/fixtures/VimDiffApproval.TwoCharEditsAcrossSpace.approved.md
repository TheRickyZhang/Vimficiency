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

### Plan 1: cost 9

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
`1 / 1 / 3`

### Plan 2: cost 9.23607

```
bc xy
->
Xc xY
```
`3.23607 / 5 / 0`

### Plan 3: cost 10

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
`2 / 3 / 1`
