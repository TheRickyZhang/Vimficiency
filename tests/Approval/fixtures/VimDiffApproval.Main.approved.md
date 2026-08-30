# per-line value edits

**initial**
```
int main() {
  int n = 0;
  for(int i = 0; i < n; i++) {
    cout << i << endl;
  }
}
```

**final**
```
int main() {
  int m = 1;
  for(int i = 0; i < m; i++) {
    cout << i+1 << endl;
  }
}
```

_Edit costs: delete / insert / move_

### Plan 1: cost 30.9

```
n = 0;
->
m = 1;
```
`2 / 8 / 0`

```
n
->
m
```
`1 / 3 / 8.2`

```
->
+1
```
`0 / 4 / 4.7`

### Plan 2: cost 30.9

```
n = 0
->
m = 1
```
`4 / 7 / 0`

```
n
->
m
```
`1 / 3 / 7.2`

```
->
+1
```
`0 / 4 / 4.7`

### Plan 3: cost 30.9

```
n
->
m
```
`1 / 3 / 0`

```
0
->
1
```
`1 / 3 / 3`

```
n
->
m
```
`1 / 3 / 7.2`

```
->
+1
```
`0 / 4 / 4.7`
