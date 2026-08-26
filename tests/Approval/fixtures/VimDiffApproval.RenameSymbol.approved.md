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

### Plan 1: cost 28.792

```
cn
->
to
```
`2 / 2 / 0`

```
cnt = cn
->
tot = to
```
`3.41421 / 8 / 3.64575`

```
cn
->
to
```
`2 / 2 / 2.73205`

### Plan 2: cost 29.3778

```
cn
->
to
```
`2 / 2 / 0`

```
cn
->
to
```
`2 / 2 / 3.64575`

```
 cn
->
 to
```
`2 / 3 / 2`

```
cn
->
to
```
`2 / 2 / 2.73205`

### Plan 3: cost 29.3778

```
cn
->
to
```
`2 / 2 / 0`

```
cn
->
to
```
`2 / 2 / 3.64575`

```
cn
->
to
```
`2 / 2 / 3`

```
cn
->
to
```
`2 / 2 / 2.73205`
