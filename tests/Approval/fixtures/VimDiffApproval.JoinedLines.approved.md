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
`1 / 2 / 0`

```


->
 
```
`1 / 2 / 1`

```


->
 
```
`1 / 2 / 2`

### Plan 2: cost 16

```

two

->
 two 
```
`3 / 6 / 0`

```


->
 
```
`1 / 2 / 2`

### Plan 3: cost 17

```


->
 
```
`1 / 2 / 0`

```


->
 
```
`1 / 2 / 1`

```
r

->
r 
```
`2 / 3 / 2`
