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

### Plan 1: cost 32

```
first old
->
abc
```
`2 / 5 / 0`

```
->
 here
```
`0 / 7 / 1`

```
second old
->
def
```
`2 / 5 / 2`

```
->
e her
```
`0 / 7 / 1`

### Plan 2: cost 32

```
first old
->
abc
```
`2 / 5 / 0`

```
->
e her
```
`0 / 7 / 1`

```
second old
->
def
```
`2 / 5 / 2`

```
->
e her
```
`0 / 7 / 1`

### Plan 3: cost 32

```
first old
->
abc
```
`2 / 5 / 0`

```
->
 here
```
`0 / 7 / 1`

```
second ol
->
```
`2 / 0 / 2`

```
->
ef
```
`0 / 4 / 1`

```
->
e her
```
`0 / 7 / 1`
