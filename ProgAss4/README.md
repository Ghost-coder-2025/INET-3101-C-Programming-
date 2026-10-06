# Colossus Airlines Reservation System: File Persistence

This is my C program for the Colossus Airlines module assignment. It manages seat reservations for an outbound flight and an inbound flight (24 seats each). In this version, the seat data is saved to a file when I quit and loaded back when I start the program again. I also used AI to generate corrupted data files to test whether my program can handle a bad file without crashing.

## Files in this repo

- `solution.c`: the full program (menus, seat functions, loading and saving)
- `fuzzer.py`: the AI-generated Python script that makes the corrupted test files
- `screenshots/`: screenshots of the program saving data and recovering from a bad file

---

## 1. Problem Statement & Persistence Design

**The problem.** Before this assignment, my program kept all the reservations in memory. When the program ended, everything was lost. The assignment was to save the data to a file when the user quits and load it again on startup, using `fopen`, `fread`, `fwrite`, and `fclose`. The program also has to keep working if the file is missing or damaged.

**How I save the data.** I used a binary file called `flight_data.dat`. My program already stores each seat in a structure with four fields: the seat number, an "assigned" flag (0 for empty, 1 for taken), a last name (up to 50 characters), and a first name (up to 50 characters). Because every seat is the same size, I can write a whole flight of 24 seats with a single `fwrite` call.

**File structure.** The file has 48 seats one after another: the 24 outbound seats first, then the 24 inbound seats. Each seat is 108 bytes on my computer, so a good file is 48 x 108 = **5184 bytes**. There is no header in the file.

**Saving.** The save function runs once after the main menu loop ends, so it happens when the user picks `c) Quit` (and also if the input ends). It opens the file for binary writing, writes outbound and then inbound, and closes the file. I check the result of `fopen`, both `fwrite` calls, and `fclose`. If something fails, it prints an error message and the program still ends normally.

**Loading.** The load function runs at the start of the program. First it sets both flights to 24 empty seats. Then it tries to open `flight_data.dat` and read the saved seats over the empty ones. If the file doesn't exist (like the first time the program runs), it prints a message and keeps the empty seats. If anything goes wrong while loading, it resets both flights to empty.

---

## 2. File Validation & Error Recovery Analysis

When the load function finds a problem, it prints a message, resets both flights to empty, and returns. It never keeps half-loaded data. These are the checks, in order:

1. **Does the file exist?** `fopen` returns `NULL` if it can't open the file. The program starts with empty flights, which is normal for the first run.
2. **Unexpected end of file (EOF).** `fread` returns how many seats it actually read. I check that it returns exactly 24 for each flight. If the file is short (empty, cut off after 10 seats, or cut off in the middle of a seat), `fread` returns less than 24 and the file is rejected.
3. **Extra bytes.** After reading all 48 seats, I read one more character. A good file has nothing left, so that read should hit the end of the file. If it finds anything else, the file is too big and is rejected.
4. **Validating the loaded seats.** I wrote two helper functions that check the data after it is loaded:
   - `check_flight` goes through all 24 seats and makes sure the seat number matches the seat's position (seat 1, then seat 2, and so on) and that the assigned flag is only 0 or 1.
   - `name_ok` checks each name. It must contain an end-of-string character (`'\0'`) within its 50 characters, and every character before that must be a normal printable character.

**About corrupted headers.** My file doesn't have a header, so there isn't one to check. Instead, the exact file size and the seat numbers (which must be 1 to 24 in order, twice) act as a check that the file has the structure I expect.

**Why the name check matters.** In C, functions that print or compare strings keep going until they find the end-of-string character. If a corrupted name never ends, they read past the end of the array into other memory. That is undefined behavior and can crash the program.

**Permission errors.** If the data file is read-only, opening it for writing fails when saving. The program prints "Could not open file for saving." and exits normally instead of crashing.

---

## 3. Pros & Cons of Solution

**Pros of saving the data as binary**

- It is short and simple. One `fwrite` or `fread` call handles a whole flight, so there is no text-parsing code to get wrong.
- The file is always exactly 5184 bytes, so it is easy to tell when a file is the wrong size.
- Names are stored in fixed-size fields, so I don't have to worry about commas, line endings, or line lengths.

**Cons of saving the data as binary**

- It is not portable. The file depends on the size of an `int`, byte order, and how the compiler pads the structure. A file from a different computer or compiler might not load.
- I can't open it in a text editor to read or fix it. I would need a tool like `xxd`.
- If I change the structure later (add a field, for example), old files stop working, and there is no version number to warn me.
- There is no checksum. If someone changes a name to different normal-looking text, my program would still accept the file.
- Opening the file for writing erases the old file right away. If the program fails halfway through writing, the old data is gone. A safer way would be to write a temporary file and rename it when done.
- If `fopen` fails for a reason other than a missing file (like a permission problem), the message still says "No saved data found," which isn't exactly right.

**Text/CSV alternative.** I could have saved one line per seat, like `5,1,Smith,Ann`. That would be easy to read and would work on any computer. But I would have to read each line with `fgets`, split it with `sscanf`, and handle bad lines, which is a lot more code. For this assignment I picked binary because it was simpler and made file size checks easy.

---

## 4. AI Fuzzing Reflection

### The prompt I used

I gave this prompt to an AI to make the fuzzer:

> I have a C program that stores flight data in a binary file called flight_data.dat: 48 records of this struct (24 outbound, then 24 inbound). The struct is: int seatID (4 bytes, offset 0), int assigned (4 bytes, offset 4), char lastname[50] (offset 8), char firstname[50] (offset 58). Each record is 108 bytes, so a valid file is 5184 bytes. Integers are little-endian.
>
> Write a Python 3 script called fuzzer.py that creates these test files in the current folder, with a comment above each one explaining what it tests:
>
> 1. valid.dat: 48 valid records (seatID 1-24 twice, assigned 0 except outbound seat 5 and inbound seat 7, which are assigned with names)
> 2. empty.dat: 0 bytes
> 3. truncated_10.dat: valid.dat cut off after 10 seats (1080 bytes)
> 4. truncated_100.dat: only the first 100 bytes
> 5. oversized.dat: valid.dat plus 50 extra bytes appended
> 6. garbage_names.dat: valid.dat with non-printable binary bytes (like \x00\x01\xff\x80) injected inside an assigned seat's name
> 7. no_terminator.dat: one assigned seat with a 50-letter name and no null byte
> 8. bad_seatid_high.dat: a seatID of 99
> 9. bad_seatid_negative.dat: a seatID of -5
> 10. bad_assigned.dat: assigned = 7
> 11. bad_assigned_negative.dat: assigned = -1
> 12. random_garbage.dat: 5184 random bytes
> 13. all_zeros.dat: 5184 zero bytes
> 14. all_ff.dat: 5184 bytes of 0xFF

The AI produced `fuzzer.py`, and when I ran it, it created all 14 files. `valid.dat` was 5184 bytes, which matched what I expected.

### What each corrupted file tests, and how my code handles it

This table shows the edge case each file represents, what could go wrong if the program trusted the file, which check in my code stops it, and what happened when I ran it.

| File | Edge case | What could go wrong without a check | Check that handles it | Result |
|---|---|---|---|---|
| valid | Normal file | Nothing | none needed | Loaded; seats 5 and 7 shown |
| empty | 0 bytes | Arrays left with random leftover values | `fread` count must be 24 | "File is too short" |
| truncated_10 | Ends after 10 seats | Only part of a flight loaded | `fread` count must be 24 | "File is too short" |
| truncated_100 | Ends in the middle of a seat | A half-filled seat loaded | `fread` count must be 24 | "File is too short" |
| oversized | 50 extra bytes | Extra data silently ignored | extra-byte check | "File is too big" |
| garbage_names | Non-printable bytes in a name | Junk printed to the terminal | `name_ok` | "File has bad data" |
| no_terminator | 50-letter name, no end marker | Printing or comparing the name reads past the end of the array | `name_ok` | "File has bad data" |
| bad_seatid_high | Seat number 99 | Array position and seat number no longer match | `check_flight` | "File has bad data" |
| bad_seatid_negative | Seat number -5 | Same as above | `check_flight` | "File has bad data" |
| bad_assigned | Flag = 7 | Seat looks taken but has no name | `check_flight` | "File has bad data" |
| bad_assigned_negative | Flag = -1 | Same as above | `check_flight` | "File has bad data" |
| random_garbage | 5184 random bytes | Right size, but nonsense in every field | `check_flight` and `name_ok` | "File has bad data" |
| all_zeros | Right size, all zeros | Every seat number is 0 | `check_flight` | "File has bad data" |
| all_ff | Right size, all 0xFF | Every number is -1 and names never end | `check_flight` and `name_ok` | "File has bad data" |

In every test the program started normally, showed empty flights (except for `valid.dat`), and exited with code 0.

### What I learned from the fuzzer

The main lesson was that `fread` returning 24 seats only tells me the file is the right size. It doesn't tell me the data makes sense. Several of the files (`bad_assigned`, `bad_seatid`, `no_terminator`, `random_garbage`, `all_zeros`, `all_ff`) are exactly 5184 bytes, so a size check alone would let them through. That is why I added `check_flight` and `name_ok` to look at the actual values after loading.

The `oversized` file taught me a different lesson. `fread` stops after 48 seats and never looks at what comes after, so I needed the extra-byte check to notice the leftover bytes.

### Other problems I ran into

- **Segmentation fault.** When I first wrote the load function, I accidentally made it call itself instead of setting the flights to empty. It called itself forever until the program crashed. I fixed it by replacing that line with calls to the function that initializes the flights.
- **Typo.** I typed `!-` instead of `!=` in one `if` statement, and the program wouldn't compile.
- **Menu confusion.** I thought `c` wasn't quitting, but I was in the flight menu, where `c` means "alphabetical list." I had to press `f` first to go back to the main menu, where `c` is Quit.

### What I would still improve

A corrupted file that still has normal-looking values (like a changed name) would pass my checks. Detecting that would need a checksum or a version number in the file.

---
