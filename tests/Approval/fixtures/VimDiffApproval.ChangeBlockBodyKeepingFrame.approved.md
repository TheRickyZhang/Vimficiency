# change block body keeping frame

**initial**
```
function f() {
  i++;
}
```

**final**
```
function f() {
  return 17;
}
```

_Edit costs: delete / insert / move_

### Plan 1: cost 14

```
i++
->
return 17
```
`3 / 11 / 0`

### Plan 2: cost 14

```
i++;
->
return 17;
```
`2 / 12 / 0`

### Plan 3: cost 15

```
  i++
->
  return 17
```
`2 / 13 / 0`
