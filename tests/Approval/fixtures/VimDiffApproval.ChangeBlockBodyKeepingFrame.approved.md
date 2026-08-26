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

### Plan 1: cost 12.7321

```
i++
->
return 17
```
`2.73205 / 9 / 0`

### Plan 2: cost 14

```
 i++
->
 return 17
```
`3 / 10 / 0`

### Plan 3: cost 14

```
i++;
->
return 17;
```
`3 / 10 / 0`
