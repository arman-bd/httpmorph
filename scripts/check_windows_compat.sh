#!/bin/bash
# Quick Windows compatibility check (no Docker required)
# Tests syntax and common Windows porting issues

set -e

echo "╔═══════════════════════════════════════════════════════════╗"
echo "║        Windows Compatibility Check (Local)               ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""

ERRORS=0
WARNINGS=0

# Test 1: Check for Windows-specific guards
echo "📋 Test 1: Windows-specific code guards"
if grep -r "#ifdef _WIN32" src/core/*.c | wc -l | grep -qv "^0"; then
    echo "   ✅ Found $(grep -r '#ifdef _WIN32' src/core/*.c | wc -l | tr -d ' ') Windows guards"
else
    echo "   ❌ No Windows guards found!"
    ERRORS=$((ERRORS + 1))
fi

# Test 2: Check for POSIX feature macros
echo ""
echo "📋 Test 2: POSIX feature macros (should be guarded)"
if grep -n "_POSIX_C_SOURCE\|_XOPEN_SOURCE" src/core/*.c | grep -v "#ifndef _WIN32"; then
    echo "   ⚠️  POSIX macros may not be properly guarded"
    WARNINGS=$((WARNINGS + 1))
else
    echo "   ✅ POSIX macros properly guarded or not present"
fi

# Test 3: Check for Windows headers
echo ""
echo "📋 Test 3: Windows header includes"
for file in src/core/*.c; do
    if grep -q "#ifdef _WIN32" "$file"; then
        if grep -q "winsock2.h" "$file"; then
            echo "   ✅ $(basename $file): Has winsock2.h"
        else
            echo "   ⚠️  $(basename $file): Missing winsock2.h"
            WARNINGS=$((WARNINGS + 1))
        fi
    fi
done

# Test 4: Check for unguarded POSIX-only functions
echo ""
echo "📋 Test 4: POSIX-only functions (should have Windows alternatives)"
POSIX_ONLY="fork|pipe|dup2|execve|waitpid"
if grep -rn "\b($POSIX_ONLY)\s*(" src/core/*.c 2>/dev/null; then
    echo "   ⚠️  Found POSIX-only functions (check if properly guarded)"
    WARNINGS=$((WARNINGS + 1))
else
    echo "   ✅ No unguarded POSIX-only functions"
fi

# Test 5: Check for proper string function replacements
echo ""
echo "📋 Test 5: String function replacements"
ISSUES=0

if grep -rn "strcasecmp" src/core/*.c | grep -v "_stricmp\|ifdef\|define"; then
    echo "   ⚠️  Found strcasecmp (should use _stricmp on Windows)"
    ISSUES=$((ISSUES + 1))
fi

if grep -rn '\bstrdup\s*(' src/core/*.c | grep -v "_strdup\|ifdef\|define\|static"; then
    echo "   ⚠️  Found strdup (should use _strdup on Windows)"
    ISSUES=$((ISSUES + 1))
fi

if [ $ISSUES -eq 0 ]; then
    echo "   ✅ String functions properly handled"
else
    WARNINGS=$((WARNINGS + ISSUES))
fi

# Test 6: Check for fcntl usage (needs ioctlsocket on Windows)
echo ""
echo "📋 Test 6: Non-blocking socket setup"
if grep -rn "fcntl.*F_SETFL\|fcntl.*F_GETFL" src/core/*.c | grep -v "ifdef"; then
    echo "   ⚠️  Found fcntl usage (check if ioctlsocket alternative exists for Windows)"
    WARNINGS=$((WARNINGS + 1))
else
    echo "   ✅ Non-blocking socket setup looks properly guarded"
fi

# Test 7: Check for clock_gettime (needs QueryPerformanceCounter on Windows)
echo ""
echo "📋 Test 7: Time functions"
if grep -rn "clock_gettime" src/core/*.c >/dev/null; then
    if grep -rn "QueryPerformanceCounter" src/core/*.c >/dev/null; then
        echo "   ✅ Found both clock_gettime and QueryPerformanceCounter"
    else
        echo "   ⚠️  Found clock_gettime but no QueryPerformanceCounter for Windows"
        WARNINGS=$((WARNINGS + 1))
    fi
else
    echo "   ✅ No time function portability issues"
fi

# Test 8: Check for errno vs WSAGetLastError
echo ""
echo "📋 Test 8: Error handling"
if grep -rn "\berrno\b" src/core/*.c | grep -q "EINPROGRESS\|EWOULDBLOCK"; then
    if grep -rn "WSAGetLastError" src/core/*.c >/dev/null; then
        echo "   ✅ Found both errno and WSAGetLastError handling"
    else
        echo "   ⚠️  Found errno but no WSAGetLastError for Windows"
        WARNINGS=$((WARNINGS + 1))
    fi
else
    echo "   ✅ Error handling looks correct"
fi

# Test 9: Check for printf format specifiers
echo ""
echo "📋 Test 9: Printf format specifiers for uint64_t"
if grep -rn 'printf.*%lu' src/core/*.c | grep -i "uint64" | grep -v "PRIu64"; then
    echo "   ⚠️  Found %lu with uint64_t (should use PRIu64)"
    WARNINGS=$((WARNINGS + 1))
else
    echo "   ✅ Printf format specifiers look correct"
fi

# Test 10: Check for strnlen (may not be available in older MSVC)
echo ""
echo "📋 Test 10: strnlen availability"
if grep -rn "\bstrnlen\s*(" src/core/*.c >/dev/null; then
    if grep -rn "_MSC_VER.*strnlen" src/core/*.c >/dev/null; then
        echo "   ✅ strnlen has fallback for older MSVC"
    else
        echo "   ⚠️  Using strnlen (may need fallback for MSVC < 2015)"
        WARNINGS=$((WARNINGS + 1))
    fi
else
    echo "   ✅ Not using strnlen"
fi

# Summary
echo ""
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║                      Summary                              ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""
echo "Errors:   $ERRORS"
echo "Warnings: $WARNINGS"
echo ""

if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo "✅ All checks passed! Windows compatibility looks good."
    echo ""
    echo "Next steps:"
    echo "  • Test in GitHub Actions with real Windows runner"
    echo "  • Or use: make docker-windows (requires Docker)"
    exit 0
elif [ $ERRORS -eq 0 ]; then
    echo "⚠️  Found $WARNINGS warnings. Review the output above."
    echo ""
    echo "These are potential issues but may be false positives."
    echo "Test with GitHub Actions for definitive results."
    exit 0
else
    echo "❌ Found $ERRORS errors. Windows build may fail."
    echo ""
    echo "Fix the errors above before pushing to CI."
    exit 1
fi
