# clean single word change

**initial**
```
the quick brown fox
jumps over
the lazy dog
```

**final**
```
the quick red fox
jumps above
the lazy cat
```

_Edit costs: delete / insert / move_

### Plan 1: cost 23

```
brown
->
red
```
`2 / 4 / 0`

```
->
ab
```
`0 / 4 / 2`

```
r
->
```
`1 / 0 / 1`

```
dog
->
cat
```
`2 / 4 / 3`

### Plan 2: cost 24

```
brown
->
red
```
`2 / 4 / 0`

```
o
->
abo
```
`1 / 4 / 2`

```
r
->
```
`1 / 0 / 1`

```
dog
->
cat
```
`2 / 4 / 3`

### Plan 3: cost 24

```
brown
->
red
```
`2 / 4 / 0`

```
 
->
 ab
```
`1 / 4 / 2`

```
r
->
```
`1 / 0 / 1`

```
dog
->
cat
```
`2 / 4 / 3`
