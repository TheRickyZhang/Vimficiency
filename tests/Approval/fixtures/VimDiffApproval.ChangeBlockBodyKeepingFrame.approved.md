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

### Plan: cost 13

```
i++;
->
return 17;
```
`2 / 11 / 0`
