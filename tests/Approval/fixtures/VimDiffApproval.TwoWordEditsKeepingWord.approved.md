# two edits keeping a whole word

**initial**
```
aaa bbb ccc
```

**final**
```
xxx bbb yyy
```

_Edit costs: delete / insert / move_

### Plan 1: cost 14

```
aaa bbb ccc
->
xxx bbb yyy
```
`2 / 11 / 0`

### Plan 2: cost 14

```
aaa
->
xxx
```
`2 / 3 / 0`

```
 ccc
->
 yyy
```
`2 / 4 / 1`

### Plan 3: cost 14

```
aaa 
->
xxx 
```
`2 / 4 / 0`

```
ccc
->
yyy
```
`2 / 3 / 1`
