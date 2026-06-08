# clean trailing deletion

**initial**
```
hello world
```

**final**
```
hello
```

_Edit costs: delete / insert / move_

### Plan 1: cost 3

```
 world
->
```
`2 / 0 / 0`

### Plan 2: cost 5

```
o world
->
o
```
`3 / 1 / 0`

### Plan 3: cost 6.41421

```
lo world
->
lo
```
`3.41421 / 2 / 0`
