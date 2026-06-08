# counted multi-line delete

**initial**
```
one
two
three
four
```

**final**
```
one
four
```

_Edit costs: delete / insert / move_

### Plan 1: cost 4.41421

```
two
three

->
```
`3.41421 / 0 / 0`

### Plan 2: cost 4.73205

```
e
two
thre
->
```
`3.73205 / 0 / 0`

### Plan 3: cost 5.41421

```

two
three
->
```
`4.41421 / 0 / 0`
