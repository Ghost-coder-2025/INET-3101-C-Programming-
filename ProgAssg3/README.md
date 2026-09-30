# Colossus Airlines Reservation System: Stream Stress Testing

## Problem & Solution Summary

This is a menu-driven C program that manages seat assignments for Colossus Airlines. It has two flights (outbound and inbound) with 24 seats each.

### Struct design

```c
struct seat {
    int seatID;
    int assigned;
    char lastname[50];
    char firstname[50];
};
```

- `seatID` is the seat number (1-24).
- `assigned` is 1 if the seat is taken and 0 if it is empty.
- `lastname` and `firstname` hold the customer's name.

`main` declares two arrays of `struct seat`, `outbound[24]` and `inbound[24]`, and one `for` loop initializes every seat in both arrays.

### Menu architecture

There are two menu levels.

- **Main menu:** a) Outbound Flight, b) Inbound Flight, c) Quit.
- **Flight menu** (the same menu for either flight, since each option takes the chosen array as a parameter): a) count empty seats, b) list empty seats, c) alphabetical list of assigned seats, d) assign a customer to a seat, e) delete a seat assignment, f) return to the main menu.

Each option is its own function:

| Function | Purpose |
|---|---|
| `read_line` | Reads one whole line safely (see below) |
| `get_choice` | Reads a menu selection, returns the letter, `0` if invalid, or `EOF` |
| `count_empty` | Counts seats with `assigned == 0` |
| `list_empty` | Prints the numbers of empty seats |
| `list_alphabetical` | Sorts assigned seats by last name (bubble sort on an array of indexes) and prints them |
| `assign_seat` | Assigns a customer to a seat, with validation |
| `delete_seat` | Clears an assigned seat, with validation |
| `flight_menu` | Second-level menu loop |

## Input Stream & Buffer Analysis

### The problem

My first version read menu choices with `scanf(" %c", &choice)`. That reads exactly one character and leaves everything else on the line in the input buffer, including the `\n`. That is fine for a single letter, but it causes trouble as soon as the program also reads a string:

- If a `scanf` for a letter is followed by a `fgets` for a name, the leftover `\n` is read by `fgets` as an empty name, so a prompt is skipped.
- If a whole line such as `John Smith` arrives where the program expects a menu letter, `scanf` takes `J`, then `o`, then `h`, and so on. Each character becomes a separate menu choice, and letters that happen to match a menu option (`d`, `e`, `a`) trigger real actions.
- A long name can overflow a fixed-size array if it is read without a size limit.

### The fix: one way to read input

Every input in the program now goes through the same function, `read_line`, which reads a whole line with `fgets`. Nothing else reads from `stdin`. Because every read consumes a full line including its `\n`, no newline is ever left behind for the next prompt, and switching between a menu letter and a name can't pollute the stream.

```c
int read_line(char *buf, int size)
{
    int len;
    int c;

    if (fgets(buf, size, stdin) == NULL)
        return -1;                 // end of input

    len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
    {
        buf[len - 1] = '\0';       // normal case: strip the newline
        return 0;
    }
    c = getchar();
    if (c == EOF)                  // last line had no trailing newline
        return 0;
    while (c != '\n' && c != EOF)  // line was too long: discard the rest
        c = getchar();
    return 1;
}
```

How each kind of input is handled:

- **Menu choices** (`get_choice`): read a whole line, and accept it only if it is exactly one character. Input like `ab`, `abc`, or a blank line is reported as an invalid choice instead of being taken apart character by character.
- **Seat numbers:** read a whole line, then convert it with `sscanf` on that line. Non-numbers (`abc`) and out-of-range numbers (`0`, `25`) are rejected.
- **Names:** read directly into the struct's 50-character field with `fgets`. A name longer than 49 characters is cut to fit, and `read_line` throws away the rest of the line so it doesn't leak into the next prompt. Names with spaces work because `fgets` reads to the end of the line.
- **Blank line:** at any prompt in the assign and delete options, a blank line cancels the operation. A half-entered assignment is cleared (the last name is reset) so no partial record is left.
- **End of input:** `read_line` returns `-1` at end of input, and `get_choice` turns that into `EOF`. Both menu loops exit on `EOF`, so a test file that ends without a quit command can't cause an infinite loop.

## AI Test Harness Evaluation

### AI prompt log: prompt used to generate `test_input.txt`

> My C program has a main menu (a = Outbound, b = Inbound, c = Quit) and a flight menu (a = count empty, b = list empty, c = alphabetical list, d = assign customer, e = delete assignment, f = return). Seats are numbered 1-24. Names are stored in 50-char fields. Write `test_input.txt` with one input per line, for `./solution < test_input.txt`, covering: assigning an occupied seat, names with spaces, names over 60 characters, deleting an unassigned seat, aborting midway through `d` and `e` (blank line), invalid menu selections at both menu levels, and ending with a clean quit. 

*(Replace this with the exact prompt you used if it was different.)*

### Test file contents

The test file covers:

- invalid choices at both menu levels (`x`, `z`, `ab`),
- assigning an occupied seat (`d`, `1` twice),
- a name with spaces (`Van Der Berg`, `Mary Ann`),
- a name over 60 characters,
- deleting an unassigned seat (`e`, `3`),
- blank-line aborts in `d` and `e`, including one after the last name has been entered,
- non-numeric and out-of-range seat numbers (`abc`, `25`),
- a final `c` to quit.

### Test case analysis (results from `test_output.log`)

| Input lines | What it tests | Observed result |
|---|---|---|
| `x` | Invalid choice, main menu | "Invalid choice." |
| `d`, `1`, `Smith`, `John` | Normal assignment | "Seat 1 assigned." |
| `d`, `1` | Occupied seat | "Seat 1 is already assigned." |
| `d`, `2`, (77-character name), `Bob` | Name over the buffer limit | Name cut to fit, "Seat 2 assigned.", no skipped prompt |
| `e`, `3` | Delete an unassigned seat | "Seat 3 is not assigned." |
| `d`, (blank) | Abort `d` at the seat prompt | Returns to the flight menu, nothing assigned |
| `e`, (blank) | Abort `e` at the seat prompt | Returns to the flight menu, nothing deleted |
| `d`, `7`, `Jones`, (blank) | Abort `d` midway (after the last name) | Returns to the flight menu, no "assigned" message, seat 7 left empty |
| `d`, `abc` / `d`, `25` | Non-numeric and out-of-range seat | "Invalid seat number." both times |
| `z`, `ab` | Invalid choices, flight menu | "Invalid choice." both times |
| `f`, `z` | Return to main menu, then invalid choice | Back at main menu, "Invalid choice." |
| `b`, `a`, `d`, `5`, `Van Der Berg`, `Mary Ann` | Inbound flight, names with spaces | "Seat 5 assigned." |
| `b`, `c`, `e`, `5` | List, alphabetical list, delete | Empty-seat list without seat 5, "Seat 5: Van Der Berg, Mary Ann", "Seat 5 assignment deleted." |
| `f`, `c` | Return, then quit | "Goodbye.", exit code 0 |

Outbound and inbound are separate arrays, which is why the inbound flight still showed 24 empty seats after outbound seats were assigned.

The first version of the test file had one line per name, but after I split the name into last-name and first-name prompts, the file needed two lines per name. I updated it to match and added the extra cases above.

### Bugs the batch test uncovered

1. **Character-by-character menu reading.** Running the test against the original `scanf(" %c", ...)` code produced a huge flood of "Invalid choice." lines. Every character of `John Smith` and of the long name was treated as a separate menu selection, and some of those characters triggered real menu options (the log showed placeholder output for options I had not typed). This is the main buffer failure the test was designed to expose.
2. **No handling of end of input.** With `scanf`, reaching the end of the redirected file leaves `choice` unchanged, so a menu loop would repeat forever if the input ended before a quit command.
3. **A last line without a trailing newline.** After the seat logic was working, the log ended with two extra "Invalid choice." lines instead of `Goodbye.`. The test file's final `c` had no newline after it, and my first `read_line` treated a line without a newline as "too long" and discarded it. I found this with `tail -5 test_input.txt | cat -et`, which showed the last line had no `$`. This was the kind of bug that only shows up with redirected files, not typed input.

### How the code was refactored

- Replaced every `scanf` with the single `read_line` function, and built `get_choice` on top of it so a menu choice is one whole validated line.
- Added `EOF` checks to both menu loops so a script that ends early exits cleanly.
- Sized the line buffer to match the name fields so `fgets` truncates long names safely, and discarded the rest of an over-long line.
- Changed `read_line` to accept a final line with no trailing newline.
- Added the cancel-on-blank-line behavior and made `assign_seat` clear the partially entered last name if the first name is cancelled.

### Result

After the refactor, the whole test script runs to the end without skipped prompts or infinite loops, the output ends with `Goodbye.`, and the exit code is `0`. A run with the final `c` removed also exits cleanly.



### AI usage

I felt I heavily relied on AI on this assignment and still don't understand the code that well but I'll spend more time on this outside of class, however I have to submit this readme file for partial points. 