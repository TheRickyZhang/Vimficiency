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

### Plan 1: cost 2

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
`2 / 2 / 0`

### Plan 3: cost 4

```
bbbbbbba
->
a
```
`2 / 2 / 0`
