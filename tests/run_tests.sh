#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail()
{
    echo "FAIL: $*" >&2
    exit 1
}

check_contains()
{
    grep -Fq "$2" "$1" || fail "$2 was not found in $1"
}

printf 'echo hello\n' | ./mysh >"$tmp/basic.out" 2>"$tmp/basic.err"
check_contains "$tmp/basic.out" "hello"

printf 'cd %s\npwd\n' "$tmp" | ./mysh >"$tmp/builtins.out" 2>"$tmp/builtins.err"
check_contains "$tmp/builtins.out" "$tmp"

printf 'printf abc > %s\nprintf def >> %s\ncat < %s\n' \
    "$tmp/output" "$tmp/output" "$tmp/output" |
    ./mysh >"$tmp/redirection.out" 2>"$tmp/redirection.err"
check_contains "$tmp/redirection.out" "abcdef"

printf 'printf "hello\\nworld\\n" | grep world\n' |
    ./mysh >"$tmp/pipe.out" 2>"$tmp/pipe.err"
check_contains "$tmp/pipe.out" "world"

touch "$tmp/one.txt" "$tmp/two.txt"
(
    cd "$tmp"
    printf 'echo *.txt\n' | "$root/mysh" >glob.out 2>glob.err
)
check_contains "$tmp/glob.out" "one.txt"
check_contains "$tmp/glob.out" "two.txt"

printf 'which echo\nwhich not_a_real_command\n' |
    ./mysh >"$tmp/which.out" 2>"$tmp/which.err"
check_contains "$tmp/which.out" "/usr/bin/echo"

printf 'echo before # ignored\n< <\necho after\n' |
    ./mysh >"$tmp/errors.out" 2>"$tmp/errors.err"
check_contains "$tmp/errors.out" "before"
check_contains "$tmp/errors.out" "after"
check_contains "$tmp/errors.err" "syntax error"

printf 'cat\n' >"$tmp/script"
./mysh "$tmp/script" </dev/null >"$tmp/file.out" 2>"$tmp/file.err"

echo "All tests passed."
