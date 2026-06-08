# rename twice keeping ' + '

**initial**
```
x + x
```

**final**
```
y + y
```

_Edit costs: delete / insert / move_

### Plan 1: cost 8

```
x + x
->
y + y
```
`2 / 5 / 0`

### Plan 2: cost 9

```
x
->
y
```
`1 / 1 / 0`

```
x
->
y
```
`1 / 1 / 3`

### Plan 3: cost 10

```
x
->
y
```
`1 / 1 / 0`

```
 x
->
 y
```
`2 / 2 / 2`
