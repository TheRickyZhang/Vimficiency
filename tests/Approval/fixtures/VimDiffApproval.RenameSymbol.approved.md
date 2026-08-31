# rename symbol across lines

**initial**
```
int cnt = 0;
cnt = cnt + 1;
return cnt;
```

**final**
```
int tot = 0;
tot = tot + 1;
return tot;
```

_Edit costs: delete / insert / move_

### Plan 1: cost 24

```
cn
->
to
```
`2 / 3 / 0`

```
cnt = cn
->
tot = to
```
`2 / 9 / 1`

```
cn
->
to
```
`2 / 3 / 2`

### Plan 2: cost 25

```
cn
->
to
```
`2 / 3 / 0`

```
cnt = cnt
->
tot = tot
```
`2 / 10 / 1`

```
cn
->
to
```
`2 / 3 / 2`

### Plan 3: cost 25

```
cnt
->
tot
```
`2 / 4 / 0`

```
cnt = cn
->
tot = to
```
`2 / 9 / 1`

```
cn
->
to
```
`2 / 3 / 2`
