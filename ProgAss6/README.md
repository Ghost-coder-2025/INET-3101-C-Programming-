# GNU Coreutils Assignment: Changing `ls` and `whoami`

## Problem Statement & Solution Overview

For this assignment I had to change two commands in GNU Coreutils 9.7. Coreutils is the C code behind a lot of the basic commands we use in the terminal.
The two changes were:

1. Make `ls` show the long listing (like `ls -l`) when I run it with nothing after it.
2. Make `whoami` print "You are: " before the username.

Here is what I did:

1. Downloaded the Coreutils 9.7 source code and unpacked it.
2. Built it on my Mac using the configure and make steps. I did these by hand instead of using the `build_on_mac.sh` script. At first I tried the newest development version from GitHub, but it would not set up on my Mac, so I switched to the 9.7 release.
3. Found the place in `ls.c` where the default output style gets chosen and changed it. Found the line in `whoami.c` that prints the username and changed that too.
4. Rebuilt the programs and ran my versions of `ls` and `whoami` to see if the output changed.
5. Unpacked a clean copy of the original 9.7 code in another folder and compared it to my edited files to make the diff below.

## C System Mechanics & Diff Analysis

### Part 1: `ls.c`

**The variables I looked at**

- `format` is a global variable (a variable declared outside any function, so the whole file can use it). It stores which output style `ls` should use. The choices are long format, one file per line, multiple columns, horizontal, and comma separated. Lots of other parts of the program check this one variable, so it basically controls everything about how `ls` prints.
- `format_opt` is a local variable inside the function that reads the command line options. It starts at -1, which I understand to mean "the user has not picked a format yet."

**How the command line flags and the default work together**

The function `decode_switches` uses `getopt_long` to go through every flag the user typed. When it sees a format flag like `-l`, it saves that choice in `format_opt`. Flags like `-C`, `-x`, `-m`, and `-1` do the same thing with their own values.

After all the flags are read, one statement decides the final value of `format`. It first checks whether `format_opt` is 0 or higher. Since the real format choices are all numbers starting at 0, that check is only true if the user typed a format flag. If they did, their flag wins. If `format_opt` is still -1, then no flag was given and the program uses a default.

**What I changed**

The original default had a long chain of conditions. It looked at which program was running (`ls`, `dir`, or `vdir`) and whether the output was going to a terminal. For `ls` that meant columns on a terminal and one file per line otherwise. I replaced that chain with a single default: long format. Flags still override it because the flag check comes first.

**Why this was all I needed to change**

I did not have to write any new printing code. Once `format` is set to long format, the rest of the program does the same things it does for `ls -l`. It asks the operating system for each file's details (permissions, owner, size, time), prints the "total" line, and then prints each file on its own line in the long style.

### Part 2: `whoami.c`

`whoami` gets the username using two system calls:

- `geteuid` returns the effective user ID, which is just a number that identifies who is running the program.
- `getpwuid` takes that number and looks it up in the system's user database. It gives back a struct (a bundle of related info about the user), or nothing if the lookup fails.

The username is stored in the `pw_name` part of that struct. The program also checks if the lookup failed and prints an error message if it did.

The original code printed the username by itself using `puts`. I changed that one line to use `printf` instead so it could print "You are: " first and then the name. I did not need to add any new headers because `stdio.h` was already included.

### Unified diff

This is the output of `diff -u` between a clean copy of the original 9.7 files and my edited files:

```diff
--- orig/coreutils-9.7/src/ls.c	2025-04-08 06:09:03
+++ coreutils-9.7/src/ls.c	2026-10-18 17:48:39
@@ -2311,11 +2311,8 @@
         }
     }
 
-  format = (0 <= format_opt ? format_opt
-            : ls_mode == LS_LS ? (stdout_isatty ()
-                                  ? many_per_line : one_per_line)
-            : ls_mode == LS_MULTI_COL ? many_per_line
-            : /* ls_mode == LS_LONG_FORMAT */ long_format);
+ 
+format = (0 <= format_opt ? format_opt : long_format);
 
   /* If the line length was not set by a switch but is needed to determine
      output, go to the work of obtaining it from the environment.  */
--- orig/coreutils-9.7/src/whoami.c	2025-01-07 06:36:53
+++ coreutils-9.7/src/whoami.c	2026-10-18 21:03:26
@@ -83,6 +83,6 @@
   if (!pw)
     error (EXIT_FAILURE, errno, _("cannot find name for user ID %ju"),
            (uintmax_t) uid);
-  puts (pw->pw_name);
+  printf ("You are: %s\n", pw->pw_name);
   return EXIT_SUCCESS;
 }

```

## Pros & Cons of Solution

**Pros**

- It was a small change, and it used code that was already in the program, so there was not much room to break things.
- The long listing shows permissions, owner, size, and date right away, which is nice if that is what you usually want.
- The flags still work. Typing `-C` or `-1` still changes the output style.

**Cons**

- Scripts that depend on the short `ls` output would break. A script that counts files or loops through file names would now get the "total" line and extra columns.
- `whoami` is supposed to give just the username. A script that saves its output as a username would now get "You are: " stuck on the front.
- My `ls` change does not check which program is running, so `dir` and `vdir` also default to long format now, not only `ls`.
- The comments at the top of `ls.c` and the `--help` text still describe the old behavior, so the documentation is out of date.
- GNU programs normally mark text shown to users so it can be translated into other languages. My new `whoami` text is not marked that way, so it cannot be translated.
- It makes my version act differently from `ls` and `whoami` on every other system, which could confuse people.

## AI Audit Reflection

**Prompts I used**

1. I pasted `ls.c` and asked for help changing it so it works like `ls -l`.
2. I asked it to explain what the default format code does, and then asked again in simpler terms: what is different between `ls` and `ls -l` when the program runs.
3. I asked it to help me find the global flags in `ls.c` and the user lookup function in `whoami.c`.
4. I asked it to audit my diff against GNU C coding style and explain how the flags and defaults work in `ls.c` and how `whoami.c` gets the user identity.

**What the AI helped me understand**

- The `format` variable is the main switch for how `ls` prints, which is why changing one default was enough.
- `format_opt` starting at -1 is how the program tells the difference between "the user chose a format" and "nothing was chosen."
- In `whoami`, the user ID is just a number, and a second call turns that number into a name.
- The audit pointed out some style problems in my edit, like indentation and an extra whitespace line, plus side effects like breaking scripts and changing `dir` and `vdir`.
- Understanding dependencies and how to use them.
**How I checked the changes myself**

- I searched the source files to confirm my edits were really saved in the 9.7 versions.
- I rebuilt the programs and ran them to see the real output instead of just trusting that the code looked right.
- I compared against an untouched copy of the 9.7 source so the diff only shows my changes.
- Some of the AI's advice did not work the first time. It told me to use `git diff`, but my folder came from a downloaded archive and had no git history, so the command compared two completely different files. I had to switch to comparing two folders instead. That taught me to check what a command is actually doing before trusting the result.

## Screenshots

1. Build finishing successfully: `screenshot/build_sc.png`
2. Modified `ls` showing the long listing by default: `screenshot/ls-l:whoami.png`
3. Modified `whoami` showing "You are: [username]": `screenshot/ls-l:whoami.png`