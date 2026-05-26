# Approval Tests

Approval tests use ApprovalTests.cpp for curated C++ output snapshots. Use them
for production C++ text formats and diagnostics where reviewing the whole output
is clearer than asserting individual fields.

A good approval test output is very easy for a human to skim. This means being very concise but dense. You do not need to add annotating fields if the spatial structure makes the distinction obvious. When in doubt, defer to the user for what makes good output if no similar references are given.

Run them with:

```bash
scripts/vimfy_tests approval
```

On mismatch, ApprovalTests.cpp writes a matching `.received.txt` file next to
the approved fixture. Review the diff, then replace the approved file only when
the new output is intentionally canonical.

Approved files are named from the GoogleTest suite and test name, for example
`TreeDiffApproval.TinyCodeBlock.approved.txt`.

To accept changed approval output and verify it in one step:

```bash
scripts/vimfy_tests approval --approve
scripts/vimfy_tests approval 'TreeDiffApproval.*' --approve
```

Generally, we only want to test C++ because that encodes the heavy machinery of our program. However, if you think there are good approval tests for something reflected in lua, you can add them here.
