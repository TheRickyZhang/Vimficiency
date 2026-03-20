---
name: no-theoretical-overflow-guards
description: Don't add overflow guards for values that can't realistically overflow (e.g. line/col in int)
type: feedback
---

Don't add overflow protection (casting to long long, etc.) for values that can't realistically overflow. Buffer line/column counts are nowhere near int limits.

**Why:** User called it "dumb" — it's unnecessary complexity for an impossible scenario.

**How to apply:** Only add overflow guards when values can actually approach type limits (e.g. accumulating large sums, multiplying user-controlled inputs). Don't guard simple coordinate arithmetic.
