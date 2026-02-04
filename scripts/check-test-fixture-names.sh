#!/bin/bash
# Check for ODR violations: test fixture names that match production type names
#
# ODR violations cause silent memory corruption when a test fixture and a
# production struct/class share the same name but different definitions.
#
# Usage: ./scripts/check-test-fixture-names.sh

set -e

# Get all test fixture names (classes inheriting from ::testing::Test)
test_fixtures=$(grep -rh "class [A-Za-z_][A-Za-z0-9_]* : public.*::testing::Test" tests/ 2>/dev/null \
    | sed -E 's/.*class ([A-Za-z_][A-Za-z0-9_]*) :.*/\1/' \
    | sort -u)

# Get all struct/class names from src/
src_types=$(grep -rhE "^(struct|class) [A-Za-z_][A-Za-z0-9_]*" src/ 2>/dev/null \
    | sed -E 's/^(struct|class) ([A-Za-z_][A-Za-z0-9_]*).*/\2/' \
    | sort -u)

# Find exact matches
collisions=""
for fixture in $test_fixtures; do
    if echo "$src_types" | grep -qx "$fixture"; then
        collisions="$collisions$fixture\n"
    fi
done

if [ -n "$collisions" ]; then
    echo "ERROR: Test fixture names collide with production types (ODR violation risk):"
    echo ""
    echo -e "$collisions" | while read -r name; do
        [ -z "$name" ] && continue
        echo "  $name"
        echo "    Test fixture: $(grep -rln "class $name : public.*::testing::Test" tests/)"
        echo "    Production:   $(grep -rln "^struct $name\|^class $name" src/ | head -1)"
        echo ""
    done
    echo "Fix: Rename test fixtures to end with 'Test' (e.g., ${name}Test)"
    exit 1
else
    echo "OK: No test fixture / production type name collisions found."
    exit 0
fi
