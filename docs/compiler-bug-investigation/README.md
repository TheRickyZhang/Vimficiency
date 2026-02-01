# Inline Function Bug Investigation

## Summary

A bug was discovered where `inline` functions returning structs with default member initializers produced garbage values when passed directly to template function arguments. Changing from `inline` to `static` fixed the issue.

## Observed Behavior

In `tests/Benchmarks/MotionOptimizerBench.cpp`:

```cpp
// BUG: inline functions produce garbage values
inline MotionOptimizerParams paramsA() {
  return MotionOptimizerParams{};
}

inline MotionOptimizerParams paramsB() {
  return MotionOptimizerParams{}.withDirectionalPruning(false);
}

// When called:
runUnifiedBenchmark<ENABLE_COMPARISON>(label, setup, paramsA(), paramsB(), ...);
```

**Symptoms:**
- `paramsA().useDirectionalPruning` returned garbage values like 160, 224, or even the loop counter (1, 5, 10, 15, 20)
- `paramsB().useDirectionalPruning` returned different garbage (96, 16, etc.)
- Debug output inside the inline functions never printed (functions appeared to not execute)
- `fMotionThreshold` was 0 instead of default value 3

**Fix:**
```cpp
// WORKS: static functions behave correctly
static MotionOptimizerParams paramsA() { ... }
static MotionOptimizerParams paramsB() { ... }
```

## Environment

- Compiler: GCC 15.2.1 (via `/usr/bin/c++`)
- Standard: C++23 (`-std=c++23`)
- Build: CMake, linking benchmark executable against `vimficiency_core.a` static library
- OS: Linux (Arch)

## Attempted Reproductions

Several test cases were created but **none reproduced the bug**:

1. `minimal_repro.cpp` - Simple inline vs static comparison
2. `complex_repro.cpp` - Added class static methods, constexpr template params, loops
3. `multifile/` - Multi-file with static library linking (plain and GoogleTest versions)
4. `InlineBugRepro.cpp` - Test within the actual project infrastructure using real headers

All tests showed correct behavior for both `inline` and `static` functions. Even after the investigation, the original benchmark file works correctly with `inline` - suggesting the bug was caused by stale build artifacts.

## Possible Causes

Since standalone reproduction failed, the issue is likely one of:

1. **ODR Violation**: The inline function may have different definitions visible in different translation units due to include order or macro differences
2. **Template Instantiation Interaction**: Something specific about how `runUnifiedBenchmark<ENABLE_COMPARISON>` is instantiated across the project
3. **Header Include Order**: The struct or template definition may be subtly different when included from different paths
4. **Compiler Bug**: Specific to this combination of factors that the standalone tests don't capture

## Current Status

- **Fix Applied**: Changed `inline` to `static` in benchmark file
- **Not Filed as Compiler Bug**: Cannot reproduce in any test case
- **Likely Cause**: Transient issue from stale object files or specific compilation state
- **Note**: After investigation, even `inline` works correctly now. The bug was likely caused by incremental build artifacts that got cleaned up during debugging

## Files

- `minimal_repro.cpp` - Simple test case (works correctly)
- `complex_repro.cpp` - Complex test case (works correctly)
- `multifile/` - Multi-file static library test (works correctly)

## Lessons Learned

1. When `inline` functions behave unexpectedly with templates, try `static` as a workaround
2. If debug output inside a function doesn't print, the function may not be executing as expected
3. Garbage values that match loop counters or other nearby data suggest memory layout issues
4. **Build artifacts matter**: Always try a clean rebuild when debugging mysterious behavior. Stale `.o` files can cause issues that disappear after recompilation
5. When a bug cannot be reproduced in a minimal test case AND disappears after investigation, it's likely a build system issue rather than a compiler bug
