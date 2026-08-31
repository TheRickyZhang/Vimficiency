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
`1 / 2 / 0`

```


->
 
```
`1 / 2 / 1`

```


->
 
```
`1 / 2 / 2`

### Plan 2: cost 14

```


->
 
```
`1 / 2 / 0`

```

t
->
 t
```
`2 / 3 / 1`

```


->
 
```
`1 / 2 / 2`

### Plan 3: cost 14

```

t
->
 t
```
`2 / 3 / 0`

```


->
 
```
`1 / 2 / 1`

```


->
 
```
`1 / 2 / 2`
