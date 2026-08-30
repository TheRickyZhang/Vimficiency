# rearrange lines

**initial**
```
#include <string>
#include <string_view>
#include <vector>
```

**final**
```
#include <vector>
#include <string>
#include <string_view>
```

_Edit costs: delete / insert / move_

### Plan 1: cost 26

```
->
vector>
#include <
```
`0 / 20 / 0`

```

#include <vector>
->
```
`3 / 0 / 3`

### Plan 2: cost 26

```
->
<vector>
#include 
```
`0 / 20 / 0`

```

#include <vector>
->
```
`3 / 0 / 3`

### Plan 3: cost 26

```
->
 <vector>
#include
```
`0 / 20 / 0`

```

#include <vector>
->
```
`3 / 0 / 3`
