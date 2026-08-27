# two edits keeping the middle line

**initial**
```
first old line
keep this untouched
second old line
```

**final**
```
abc line here
keep this untouched
def line here
```

_Edit costs: delete / insert / move_

### Plan 1: cost 34

```
first old
->
abc
```
`2 / 4 / 0`

```


->
 here

```
`1 / 7 / 1`

```
second old lin
->
def line her
```
`2 / 13 / 1`

### Plan 2: cost 34

```
first old
->
abc
```
`2 / 4 / 0`

```
->
 here
```
`0 / 7 / 1`

```
second old lin
->
def line her
```
`2 / 13 / 2`

### Plan 3: cost 34

```
first old lin
->
abc line her
```
`2 / 13 / 0`

```
second old lin
->
def line her
```
`2 / 13 / 2`
