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

### Plan 1: cost 8.73205

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
`1 / 1 / 2.73205`

### Plan 2: cost 10

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
`2 / 2 / 2`

### Plan 3: cost 10

```
ab
->
Xb
```
`2 / 2 / 0`

```
f
->
Y
```
`1 / 1 / 2`
