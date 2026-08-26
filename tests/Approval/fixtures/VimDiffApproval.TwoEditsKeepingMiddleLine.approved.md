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

### Plan 1: cost 30.8284

```
first old
->
abc
```
`3.41421 / 3 / 0`

```
->
 here
```
`0 / 5 / 1`

```
second old
->
def
```
`3.41421 / 3 / 2`

```
->
e her
```
`0 / 5 / 1`

### Plan 2: cost 30.8284

```
first old
->
abc
```
`3.41421 / 3 / 0`

```
->
 here
```
`0 / 5 / 1`

```
second old
->
def
```
`3.41421 / 3 / 2`

```
->
 here
```
`0 / 5 / 1`

### Plan 3: cost 31.8284

```
first old
->
abc
```
`3.41421 / 3 / 0`

```
->
 here
```
`0 / 5 / 1`

```
second old 
->
def 
```
`3.41421 / 4 / 2`

```
->
e her
```
`0 / 5 / 1`
