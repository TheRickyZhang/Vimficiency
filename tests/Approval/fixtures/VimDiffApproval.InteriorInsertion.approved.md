# clean interior insertion

**initial**
```
abc
```

**final**
```
abXc
```

_Edit costs: delete / insert / move_

### Plan 1: cost 2

```
->
X
```
`0 / 1 / 0`

### Plan 2: cost 4

```
b
->
bX
```
`1 / 2 / 0`

### Plan 3: cost 4

```
c
->
Xc
```
`1 / 2 / 0`
