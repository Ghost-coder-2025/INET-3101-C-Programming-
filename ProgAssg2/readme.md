# INET 3101 — AI Code Audit: to_base_n

## Problem & Audit Summary

The starter code was supposed to convert a number into another base (like octal or hex) recursively, but it had a few bugs in it that I had to track down.

**Bug 1: No bounds checking on base.**
The original function didn't check whether `base` was actually a valid value. Valid bases are 2-16, but if you passed in something like 20, the code just tried to run anyway and gave garbage output instead of an error. I fixed this by adding an `if (base < 2 || base > 16)` check that prints an error message and returns right away before anything else happens.

**Bug 2: Digits over 9 print wrong.**
This one was subtle. The original code used `printf("%d", r)` to print each digit. The problem is `%d` prints numbers in decimal, so if a digit's value was 10 (which should be `a` in hex), it printed the actual characters "1" and "0" instead of just the letter `a`. So a hex number that should've looked like `81` might've come out looking like `12811` or something equally wrong. To fix it I made a string `"0123456789abcdef"` and used the digit's value as an index into that string (`digits[r]`), then printed it as a single character with `%c` instead of `%d`.

**Bug 3: The prefix (0 for octal, 0x for hex) was missing/wrong.**
This was honestly the bug that took me the longest to actually understand. At first I just tried sticking the prefix-printing code at the bottom of the recursive function, but that function calls itself multiple times for numbers with more than one digit — so the prefix ended up printing once per recursive call instead of once total. For `to_base_n(129, 16)` that meant I'd get something like `0x0x81` instead of `0x81`. I only caught this by tracing through the recursion by hand and realizing the "print prefix" line was sitting inside code that runs repeatedly. The fix was to pull the prefix logic out into its own separate function (`print_with_prefix`) that runs once, prints the prefix if needed, and then calls the actual recursive digit-printing function.


### Final code

```c
#include <stdio.h>

void to_base_n(int num, int base) {
    const char digits[] = "0123456789abcdef";
    int r = num % base;

    if (num >= base) {
        to_base_n(num / base, base);
    }

    printf("%c", digits[r]);
}

void print_with_prefix(int num, int base) {
    if (base < 2 || base > 16) {
        printf("Error! base must be between 2 and 16\n");
        return;
    }

    if (base == 8) printf("0");
    if (base == 16) printf("0x");

    to_base_n(num, base);
}

int main(void) {
    printf("Testing Base 8 (21): ");
    print_with_prefix(21, 8);       // 025
    printf("\n");

    printf("Testing Base 16 (129): ");
    print_with_prefix(129, 16);     // 0x81
    printf("\n");

    printf("Testing Invalid Base (129, 20): ");
    print_with_prefix(129, 20);     // Error message
    printf("\n");

    return 0;
}
```

---

## Call-Stack Tracing

I picked `to_base_n(129, 16)` since it's the trickiest test case (recursive + prefix).

Before recursion even starts, `print_with_prefix` checks that base 16 is valid, then prints `"0x"`. That only happens once, which is the whole point of splitting it into its own function. Then `to_base_n(129, 16)` gets called.

**Going down the call stack:**

| Call | num | base | r = num % base | num >= base? | what happens |
|------|-----|------|-----------------|--------------|--------------|
| Call 1 (outer) | 129 | 16 | 1 | 129 ≥ 16, yes | calls itself again before printing anything |
| Call 2 (inner) | 8 | 16 | 8 | 8 ≥ 16, no | this is as deep as it goes — stops recursing |

So at the deepest point, the stack looks like:

```
Call 1: num=129, r=1  (still waiting on its recursive call to finish)
   Call 2: num=8, r=8  (bottom — nothing left to divide)
```

**Coming back up (unwinding):**

Call 2 finishes first since it never recursed further — it hits `printf("%c", digits[8])`, which prints `8`.

Then control goes back up to Call 1, which finishes its own line: `printf("%c", digits[1])`, which prints `1`.

So the order things actually print in is: `"0x"` (before any of this even started) → `8` (from the innermost call) → `1` (from the outer call).

**Final output: `0x81`** — matches what the test case expects.

What clicked for me here is that the print statement comes *after* the recursive call in the code, so the deepest/last call actually prints first, and then each call prints on the way back up. That's why the digits come out in the right order (most significant digit first) even though the remainder you calculate at each step is technically the least significant leftover digit at that point.

---

## AI Tool Reflection

I used Claude to help debug this, going back and forth over a few messages instead of just taking the first answer it gave.

**Good suggestions the AI gave:**
- It caught the missing bounds check right away and gave me the right fix for it.
- It correctly explained why `%d` was the wrong format specifier for printing hex digits over 9, and suggested the `digits[]` lookup string approach, which ended up being the right call.
- When I asked it to explain the division/remainder logic, it walked through why `num % base` gives you a digit and `num / base` strips it off, which actually helped me understand the recursion instead of just copying code.

**Where it messed up / where I had to catch things myself:**
- At one point the AI itself gave me a "fixed" version that still had the prefix bug — it put the prefix print statement inside the recursive function again, which meant it would've printed the prefix multiple times for multi-digit numbers. It didn't catch this on its own; I only found it by manually tracing the call stack step by step and noticing the output wouldn't match what was expected.
- In one version I edited myself, I typo'd the lookup string as `"0123456789abcedef"` (letters out of order) and asked the AI to check it — it did catch that one correctly, but it showed me how easy it is to quietly break a lookup table without noticing, since it still compiles fine.
- The AI also tended to over-engineer things — adding fixes for negative numbers, zero, integer overflow, multiple base prefixes, etc., when I only needed the three specific test cases to work. I had to ask it to simplify more than once.

**Overall:** the AI was genuinely useful for pointing out what category of bug I was looking at and for explaining C syntax/behavior I didn't fully get (like `%d` vs `%c`, or how `return` creates dead code after it). But it wasn't reliable on its own for the trickiest bug in this assignment — it made basically the same "runs once vs. runs on every recursive call" mistake itself before I caught it by tracing the code by hand. That's probably the biggest thing I took away from this: I couldn't just trust the AI's fix without actually running through the logic myself.
