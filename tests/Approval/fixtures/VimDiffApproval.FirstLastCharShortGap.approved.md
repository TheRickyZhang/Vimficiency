# first+last char, short kept gap

**initial**
```
ab cd ef
```

**final**
```
Xb cd eY
```

_Edit costs: delete / insert / move_

### Plan 1: cost 11

```
ab cd ef
->
Xb cd eY
```
`2 / 8 / 0`

### Plan 2: cost 11

```
a
->
X
```
`1 / 1 / 0`

```
f
->
Y
```
`1 / 1 / 5`

### Plan 3: cost 12

```
a
->
X
```
`1 / 1 / 0`

```
ef
->
eY
```
`2 / 2 / 4`
