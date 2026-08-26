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

### Plan 1: cost 23.7321

```
brown
->
red
```
`2 / 3 / 0`

```
->
ab
```
`0 / 2 / 2.73205`

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
`2 / 3 / 3`

### Plan 2: cost 24.4142

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
`0 / 2 / 2.41421`

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
`2 / 3 / 3`

### Plan 3: cost 24.7321

```
brown
->
red
```
`2 / 3 / 0`

```
->
ab
```
`0 / 2 / 2.73205`

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
