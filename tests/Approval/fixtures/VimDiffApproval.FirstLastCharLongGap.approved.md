# first+last char, long kept gap

**initial**
```
foo bar baz
```

**final**
```
Xoo bar baY
```

_Edit costs: delete / insert / move_

### Plan 1: cost 8.73205

```
f
->
X
```
`1 / 1 / 0`

```
z
->
Y
```
`1 / 1 / 2.73205`

### Plan 2: cost 10.7321

```
f
->
X
```
`1 / 1 / 0`

```
az
->
aY
```
`2 / 2 / 2.73205`

### Plan 3: cost 10.7321

```
fo
->
Xo
```
`2 / 2 / 0`

```
z
->
Y
```
`1 / 1 / 2.73205`
