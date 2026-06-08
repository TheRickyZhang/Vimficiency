# span-local delete inside one word

**initial**
```
abbbbbbba
```

**final**
```
aa
```

_Edit costs: delete / insert / move_

### Plan 1: cost 3

```
bbbbbbb
->
```
`2 / 0 / 0`

### Plan 2: cost 4

```
abbbbbbb
->
a
```
`2 / 1 / 0`

### Plan 3: cost 4

```
bbbbbbba
->
a
```
`2 / 1 / 0`
