# three edits over short gaps

**initial**
```
ehfaf beeac edgae
```

**final**
```
effaf eeac gdgae
```

_Edit costs: delete / insert / move_

### Plan 1: cost 10

```
h
->
f
```
`1 / 1 / 0`

```
b
->
```
`1 / 0 / 1`

```
e
->
g
```
`1 / 1 / 1`

### Plan 2: cost 12

```
h
->
f
```
`1 / 1 / 0`

```
b
->
```
`1 / 0 / 1`

```
 e
->
 g
```
`2 / 2 / 1`

### Plan 3: cost 12

```
h
->
f
```
`1 / 1 / 0`

```
be
->
e
```
`2 / 1 / 1`

```
e
->
g
```
`1 / 1 / 1`
