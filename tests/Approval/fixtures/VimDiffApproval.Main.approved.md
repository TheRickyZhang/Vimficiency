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

### Plan 1: cost 23.1099

```
n
->
m
```
`1 / 1 / 0`

```
0
->
1
```
`1 / 1 / 2.73205`

```
n
->
m
```
`1 / 1 / 4.64575`

```
->
+1
```
`0 / 2 / 3.73205`

### Plan 2: cost 23.6139

```
n = 0
->
m = 1
```
`3.23607 / 5 / 0`

```
n
->
m
```
`1 / 1 / 4.64575`

```
->
+1
```
`0 / 2 / 3.73205`

### Plan 3: cost 24.1099

```
n
->
m
```
`1 / 1 / 0`

```
0
->
1
```
`1 / 1 / 2.73205`

```
n;
->
m;
```
`2 / 2 / 4.64575`

```
->
+1
```
`0 / 2 / 2.73205`
