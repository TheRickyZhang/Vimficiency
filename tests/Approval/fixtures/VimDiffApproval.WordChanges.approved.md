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

### Plan 1: cost 25

```
brown
->
red
```
`2 / 5 / 0`

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
`2 / 5 / 3`

### Plan 2: cost 26

```
brown
->
red
```
`2 / 5 / 0`

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
`2 / 6 / 3`

### Plan 3: cost 26

```
brown 
->
red 
```
`2 / 6 / 0`

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
`2 / 5 / 3`
