# first+last char, long kept gap

**initial**
```
foo bar baz
```

**final**
```
Xoo bar baY
```

_Edit costs: delete / insert / move_

### Plan 1: cost 13

```
f
->
X
```
`1 / 1 / 0`

```
z
->
Y
```
`1 / 1 / 7`

### Plan 2: cost 14

```
foo bar baz
->
Xoo bar baY
```
`2 / 11 / 0`

### Plan 3: cost 14

```
f
->
X
```
`1 / 1 / 0`

```
baz
->
baY
```
`2 / 3 / 5`
