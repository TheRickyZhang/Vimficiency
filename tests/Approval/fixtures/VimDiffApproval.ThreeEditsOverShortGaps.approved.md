# three edits over short gaps

**initial**
```
ehfaf beeac edgae
```

**final**
```
effaf eeac gdgae
```

_Edit costs: delete / insert / move_

### Plan 1: cost 15.7321

```
hfaf beeac e
->
ffaf eeac g
```
`3.73205 / 11 / 0`

### Plan 2: cost 16.4142

```
hfaf beeac 
->
ffaf 
```
`3.41421 / 5 / 0`

```
->
eac g
```
`0 / 5 / 1`

### Plan 3: cost 16.4142

```
hfaf beeac
->
ffaf
```
`3.41421 / 4 / 0`

```
->
eac g
```
`0 / 5 / 2`
