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

### Plan 1: cost 15

```


->
 
```
`1 / 3 / 0`

```


->
 
```
`1 / 3 / 1`

```


->
 
```
`1 / 3 / 2`

### Plan 2: cost 16

```

two

->
 two 
```
`3 / 7 / 0`

```


->
 
```
`1 / 3 / 2`

### Plan 3: cost 17

```


->
 
```
`1 / 3 / 0`

```


->
 
```
`1 / 3 / 1`

```
r

->
r 
```
`2 / 4 / 2`
