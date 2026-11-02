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

### Plan: cost 30

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
second old
->
def
```
`2 / 4 / 2`

```
->
 here
```
`0 / 7 / 1`
