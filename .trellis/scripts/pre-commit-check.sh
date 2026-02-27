#!/bin/bash
# Pre-commit check script for ecc-tools project
# Validates code against project constraints before commit

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Counters
ERRORS=0
WARNINGS=0

echo "=========================================="
echo "Pre-Commit Checks for ecc-tools"
echo "=========================================="
echo ""

# Get list of changed C++ files
CHANGED_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cc|hh)$' || true)

if [ -z "$CHANGED_FILES" ]; then
    echo -e "${GREEN}✓ No C++ files to check${NC}"
    exit 0
fi

echo "Checking files:"
echo "$CHANGED_FILES" | sed 's/^/  - /'
echo ""

# Check 1: File extension validation
echo "1. Checking file extensions (.cc/.hh only)..."
WRONG_EXT=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|hpp|h|cxx)$' || true)
if [ -n "$WRONG_EXT" ]; then
    echo -e "${RED}✗ FAIL: Wrong file extensions found${NC}"
    echo "$WRONG_EXT" | sed 's/^/    /'
    echo "  Required: .cc (source) and .hh (header)"
    echo "  See: .trellis/spec/project-constraints.md § File Naming"
    ERRORS=$((ERRORS + 1))
else
    echo -e "${GREEN}✓ PASS: All files use .cc/.hh extensions${NC}"
fi
echo ""

# Check 2: Copyright header validation
echo "2. Checking copyright headers (Mulan PSL v2)..."
MISSING_COPYRIGHT=""
for file in $CHANGED_FILES; do
    if [ -f "$file" ]; then
        if ! head -20 "$file" | grep -q "Mulan PSL v2"; then
            MISSING_COPYRIGHT="$MISSING_COPYRIGHT\n    $file"
        fi
    fi
done

if [ -n "$MISSING_COPYRIGHT" ]; then
    echo -e "${RED}✗ FAIL: Missing copyright headers${NC}"
    echo -e "$MISSING_COPYRIGHT"
    echo "  Required: Mulan PSL v2 header (lines 1-16)"
    echo "  See: .trellis/spec/project-constraints.md § Copyright"
    ERRORS=$((ERRORS + 1))
else
    echo -e "${GREEN}✓ PASS: All files have copyright headers${NC}"
fi
echo ""

# Check 3: Header guard validation (#pragma once)
echo "3. Checking header guards (#pragma once)..."
WRONG_GUARDS=""
for file in $CHANGED_FILES; do
    if [[ "$file" == *.hh ]]; then
        if [ -f "$file" ]; then
            if ! grep -q "#pragma once" "$file"; then
                WRONG_GUARDS="$WRONG_GUARDS\n    $file"
            fi
            # Check for traditional guards
            if grep -q "#ifndef.*_H\|#define.*_H" "$file"; then
                WRONG_GUARDS="$WRONG_GUARDS\n    $file (uses traditional guard)"
            fi
        fi
    fi
done

if [ -n "$WRONG_GUARDS" ]; then
    echo -e "${RED}✗ FAIL: Missing or wrong header guards${NC}"
    echo -e "$WRONG_GUARDS"
    echo "  Required: #pragma once"
    echo "  See: .trellis/spec/project-constraints.md § Header Guard"
    ERRORS=$((ERRORS + 1))
else
    echo -e "${GREEN}✓ PASS: All headers use #pragma once${NC}"
fi
echo ""

# Check 4: clang-format validation
echo "4. Checking code formatting (clang-format)..."
if command -v clang-format &> /dev/null; then
    FORMAT_ISSUES=""
    for file in $CHANGED_FILES; do
        if [ -f "$file" ]; then
            # Check if file needs formatting
            if ! clang-format --dry-run -Werror "$file" 2>/dev/null; then
                FORMAT_ISSUES="$FORMAT_ISSUES\n    $file"
            fi
        fi
    done

    if [ -n "$FORMAT_ISSUES" ]; then
        echo -e "${YELLOW}⚠ WARNING: Files need formatting${NC}"
        echo -e "$FORMAT_ISSUES"
        echo "  Run: clang-format -i <file>"
        echo "  Or: git diff --name-only | grep -E '\.(cc|hh)$' | xargs clang-format -i"
        WARNINGS=$((WARNINGS + 1))
    else
        echo -e "${GREEN}✓ PASS: All files are properly formatted${NC}"
    fi
else
    echo -e "${YELLOW}⚠ WARNING: clang-format not found, skipping format check${NC}"
    WARNINGS=$((WARNINGS + 1))
fi
echo ""

# Check 5: Doxygen comment validation
echo "5. Checking Doxygen comments..."
MISSING_DOXYGEN=""
for file in $CHANGED_FILES; do
    if [ -f "$file" ]; then
        # Check for @file tag
        if ! head -30 "$file" | grep -q "@file"; then
            MISSING_DOXYGEN="$MISSING_DOXYGEN\n    $file (missing @file)"
        fi
    fi
done

if [ -n "$MISSING_DOXYGEN" ]; then
    echo -e "${YELLOW}⚠ WARNING: Missing Doxygen comments${NC}"
    echo -e "$MISSING_DOXYGEN"
    echo "  Required: @file, @author, @date, @brief"
    echo "  See: .trellis/spec/project-constraints.md § Copyright"
    WARNINGS=$((WARNINGS + 1))
else
    echo -e "${GREEN}✓ PASS: All files have Doxygen comments${NC}"
fi
echo ""

# Check 6: Forbidden patterns
echo "6. Checking for forbidden patterns..."
FORBIDDEN_FOUND=""

for file in $CHANGED_FILES; do
    if [ -f "$file" ]; then
        # Check for global LOG_* macros in iCTS module
        if [[ "$file" == *"iCTS"* ]]; then
            if grep -n "LOG_INFO\|LOG_WARNING\|LOG_ERROR\|LOG_FATAL" "$file" | grep -v "CTS_LOG" | grep -v "^[[:space:]]*//"; then
                FORBIDDEN_FOUND="$FORBIDDEN_FOUND\n    $file: Uses global LOG_* instead of CTS_LOG_*"
            fi
        fi

        # Check for using namespace in headers
        if [[ "$file" == *.hh ]]; then
            if grep -n "^using namespace" "$file" | grep -v "^[[:space:]]*//"; then
                FORBIDDEN_FOUND="$FORBIDDEN_FOUND\n    $file: 'using namespace' in header"
            fi
        fi

        # Check for C-style casts
        if grep -n "([a-zA-Z_][a-zA-Z0-9_]*\s*\*\s*)" "$file" | grep -v "^[[:space:]]*//"; then
            FORBIDDEN_FOUND="$FORBIDDEN_FOUND\n    $file: Possible C-style cast (use static_cast)"
        fi
    fi
done

if [ -n "$FORBIDDEN_FOUND" ]; then
    echo -e "${YELLOW}⚠ WARNING: Forbidden patterns found${NC}"
    echo -e "$FORBIDDEN_FOUND"
    echo "  See: .trellis/spec/backend/quality-guidelines.md § Forbidden Patterns"
    WARNINGS=$((WARNINGS + 1))
else
    echo -e "${GREEN}✓ PASS: No forbidden patterns found${NC}"
fi
echo ""

# Summary
echo "=========================================="
echo "Summary"
echo "=========================================="
echo "Files checked: $(echo "$CHANGED_FILES" | wc -l)"
echo -e "Errors: ${RED}$ERRORS${NC}"
echo -e "Warnings: ${YELLOW}$WARNINGS${NC}"
echo ""

if [ $ERRORS -gt 0 ]; then
    echo -e "${RED}✗ Pre-commit checks FAILED${NC}"
    echo "Please fix the errors above before committing."
    echo ""
    echo "For more information, see:"
    echo "  - .trellis/spec/project-constraints.md"
    echo "  - .trellis/spec/backend/quality-guidelines.md"
    exit 1
elif [ $WARNINGS -gt 0 ]; then
    echo -e "${YELLOW}⚠ Pre-commit checks passed with warnings${NC}"
    echo "Consider fixing the warnings above."
    echo ""
    echo "Continue with commit? (y/n)"
    read -r response
    if [[ ! "$response" =~ ^[Yy]$ ]]; then
        echo "Commit cancelled."
        exit 1
    fi
else
    echo -e "${GREEN}✓ All pre-commit checks passed!${NC}"
fi

exit 0
