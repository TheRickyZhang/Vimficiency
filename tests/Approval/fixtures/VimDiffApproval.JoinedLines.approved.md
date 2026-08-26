# join lines

**initial**
```
one
two
three
four
five
```

**final**
```
one two three
four five
```

_Edit costs: delete / insert / move_

### Plan 1: cost 12

```


->
 
```
`1 / 1 / 0`

```


->
 
```
`1 / 1 / 1`

```


->
 
```
`1 / 1 / 2`

### Plan 2: cost 14

```


->
 
```
`1 / 1 / 0`

```


->
 
```
`1 / 1 / 1`

```
r

->
r 
```
`2 / 2 / 2`

### Plan 3: cost 14

```

two

->
 two 
```
`3 / 5 / 0`

```


->
 
```
`1 / 1 / 2`
