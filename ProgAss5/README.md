# Module Assignment: C Socket File Transfer & AI Network Security Audit

This project is a TCP file server (`server.c`) and a client (`client.c`) written in C using BSD sockets. I also used an AI to write a fuzzer (`fuzzer.py`) that attacks my server, so I could find problems and fix them. Screenshots of everything running are in the `screenshots/` folder.

---

## 1. Problem Statement & Architecture

### The problem

The client asks the server for a file by name. If the file is 255 bytes or smaller, the server sends back the contents. If it isn't, the server sends an error message instead. The server has to keep running even when a client sends bad or strange data, and it has to close its port cleanly when I press Ctrl+C.

### How the sockets are set up

- **TCP over IPv4.** I used TCP because it is reliable and keeps data in order, so I don't have to deal with lost or shuffled packets.
- **Server steps.** The server creates a socket, turns on address reuse, binds to a port, and listens. Then it loops forever: accept one client, handle that client, close the connection.
- **One client at a time.** This is called an iterative server. All the work for one client happens inside one function. If anything goes wrong, that function just returns, and the main loop closes the socket. This means the client socket is closed in only one place, so I can't forget to close it on an error path.
- **Client steps.** The client creates a socket, connects to the server, asks the user for a filename, sends it, and prints whatever comes back until the server closes the connection.

### Protocol

| Direction | What it looks like |
|---|---|
| Request | The filename followed by a newline, at most 255 bytes in total including the newline |
| Success response | The file contents (0 to 255 bytes) |
| Error response | One line that starts with `ERROR: ` |
| End of response | The server closes the connection |

The error messages are `ERROR: Invalid filename`, `ERROR: File not found`, `ERROR: File too large (max 255 bytes)`, and `ERROR: Request too long`.

I picked a plain text request that ends with a newline because it is easy to know where the request stops, and I can test it by hand with netcat. Closing the connection to end the response keeps the client simple, because it just reads until the server hangs up.

### Port management

- The default port is 9090 for both programs, and you can change it with a command-line argument. The client also lets you give the server's address.
- I did not use port 8080. When I first tested my client, something else (an nginx web server) was already using 8080 on my computer, and it answered my client with an HTTP "400 Bad Request". So I switched to 9090.
- Ports below 1024 need admin rights, so I stayed above that.
- The server turns on the address reuse option, so I can restart it right away without getting "Address already in use".

### Shutting down with Ctrl+C

The server catches SIGINT (what Ctrl+C sends). The signal handler only sets a flag. A handler can interrupt the program at any moment, so it should do as little as possible, and setting a flag is safe. I set it up so that Ctrl+C interrupts the `accept` call instead of restarting it. The main loop then sees the flag, stops, closes the listening socket (which frees the port), and prints a shutdown message. I also ignore SIGPIPE so the server doesn't die if a client disconnects while the server is sending.

---

## 2. Socket Security & Buffer Analysis

### TCP packet framing

TCP is a stream of bytes, not separate messages. One send can show up as several receives, and several sends can get merged into one. I wrote the code with that in mind.

- **Requests:** the server reads in a loop and adds each piece to a buffer until it sees a newline. This way a filename that arrives in two pieces still works. If the buffer fills up and there is still no newline, the server rejects the request as too long instead of waiting forever.
- **Responses:** the server marks the end of a response by closing the connection. The client keeps receiving until the result is zero, and it prints exactly the number of bytes it got instead of treating the data like a string.

### Preventing string buffer overflows

- **Limited reads:** every `recv` call is limited to the space left in the buffer, and I always keep one byte free for the end-of-string character. A client can't write past the end of the buffer no matter how much it sends.
- **Fixed size:** the request buffer is 256 bytes and the loop stops at 255, so a flood of 10,000 characters gets cut off safely.
- **Ending the string myself:** I replace the newline with a null byte, so the filename is a proper C string before any string function uses it. I never assume the sender ended it for me.
- **Rejecting null bytes:** string functions stop at the first null byte. So if a client sends `test.txt`, then a null byte, then `../../etc/passwd`, the server would only see `test.txt` and never check the rest. To stop this, the server looks through the raw bytes it received (using the byte count, not string functions) and rejects the request if it finds a null byte.
- **Checking file size safely:** the file buffer is one byte bigger than the 255-byte limit. If reading the file fills the whole buffer, the file is too big. This way I can find oversized files without reading them completely.
- **Sending by length:** I send file contents using the number of bytes that were read, not by measuring a string.
- **Unsafe functions:** I didn't use `gets`, `strcpy`, or `sprintf`.

### Stopping path traversal

A filename is only accepted if all of these are true:

- It is not empty and doesn't start with a dot. This blocks `..` and hidden files.
- It has no forward slash and no backslash.
- It has no control characters (bytes below 32).

The idea is that a name with no slash can't point outside the server's folder. This blocks `../../etc/passwd`, absolute paths like `/etc/passwd`, and mixes like `subdir/../test.txt`.

I decided to **reject** bad names instead of trying to remove the bad parts. Removing parts doesn't work well: if you strip `..` out of `....//`, what is left is `../`, which is the attack again. Rejecting the whole request avoids that.

After the name passes, the server checks that the file is a regular file. If someone asks for a folder name, they get "File not found" instead of an empty reply.

### Other protection

- **Timeout:** each client socket has a 5-second receive timeout, so a client that connects and says nothing can't freeze the server forever.
- **One place to close:** every error path goes back to the main loop, which closes the client socket, so no socket gets left open.

### Things I know are still weak

The 5-second timeout applies to each receive, not to the whole request. So a client that sends one byte every few seconds can still keep the server busy for a long time. The server also follows symbolic links in its folder, and there is a tiny gap between checking a file and opening it. My fuzzer didn't test these, and they are listed again in the cons below.

---

## 3. Pros & Cons of Solution

| Pros | Cons |
|---|---|
| Simple: no threads or shared data, so no problems between clients | One slow client delays everyone else, for up to 5 seconds |
| Easy to understand, test, and shut down cleanly | A client that sends one byte every few seconds can hold the server much longer, because the timeout is per receive |
| Every error path ends at one place that closes the client socket | Only one connection at a time, so it is not fast |
| Careful input checks and limited reads, with no overflow that I know of | It serves any regular file in its folder, so it should run from its own folder |
| Small code that is easy to read and check | Symbolic links are followed, and there is a small gap between checking and opening a file |
| Passed all 16 fuzzer tests after I fixed it | Plain text only: no encryption, no login, IPv4 only, 255-byte file limit |

### Iterative vs. forking or threaded servers

A server that forks or uses threads (or uses poll or select) can handle many clients at once, so one slow client doesn't block the others. But it is more complicated. A forking server has to clean up finished child processes so they don't become zombies. A threaded server has to protect shared data, and it is harder to shut down cleanly. Signals also get trickier in both. Since this server only sends files of 255 bytes or less, I chose the simple iterative design because it is easier to get right and easier to secure. If it needed to handle real traffic, I would switch to forking or poll and add a total time limit for each request.

---

## 4. AI Fuzzing Reflection

### Prompts used

I used an AI assistant (Claude) to generate the fuzzer. I gave it the fuzzer requirement from the assignment, lightly shortened:

> Write a test script named fuzzer.py designed to craft malicious raw TCP socket payloads against the server. The fuzzer must simulate: Path Traversal Attacks (requesting filenames like ../../etc/passwd or absolute paths); Buffer Overflow/Payload Flooding (sending 10,000 characters without a newline or null terminator); Abrupt Disconnections (opening a connection and closing it immediately before sending data, or disconnecting mid-transmission); and Null Byte Injection (inserting binary null bytes inside requested file strings).

The AI gave me a Python script that runs 16 tests covering those four kinds of attacks. It also checks after every test that the server is still answering, so a test can fail because the server hung or died, not only because it returned the wrong data.

After that I used these follow-up prompts to fix the server:

1. I ran the fuzzer against my original server and gave the AI the full output (3 of 16 tests failed).
2. I asked the AI to explain each change it made to the server code, why, and where in my file each change goes.
3. After updating `server.c`, I ran the fuzzer again to make sure the failures were fixed.

### What the fuzzer does

The fuzzer runs 16 tests in four groups. After every test it sends a normal request to check that the server is still alive. A test passes if the server refuses or drops the bad input and keeps working. It fails if the server returns file data for a bad request, hangs, or stops responding.

| Group | Tests | Examples |
|---|---|---|
| Path traversal | 5 | `../../etc/passwd`, `/etc/passwd`, the `....//` trick, `subdir/../test.txt`, just `..` |
| Flooding and garbage | 3 | 10,000 characters with no newline, 10,000 characters with a newline, 1,000 random bytes |
| Null byte injection | 4 | A valid name then a null byte then a traversal path, a valid name then a null byte, one null byte alone, 300 null bytes |
| Sudden disconnects | 4 | Connect and close right away, half a filename then close, connection reset in the middle, a connection that sends nothing |

### Results against my original server

3 of the 16 tests failed:

| Failed test | What happened |
|---|---|
| Null byte after a valid name, then a traversal path | The server sent back the contents of `test.txt` |
| Null byte after a valid name | The server sent back the contents of `test.txt` |
| Connection that sends nothing | The server got stuck waiting and couldn't serve other clients |

The other 13 tests passed, including all five path traversal tests and both flooding tests.

### Vulnerabilities uncovered

| Problem | Cause | What it caused |
|---|---|---|
| Null byte injection | String functions stop at the first null byte, so everything after it was ignored and the request looked like just the valid name | The server accepted a bad request, and its filename checks never saw the hidden part |
| Idle client hang | One `recv` call that waits forever with no timeout | One silent connection froze the whole server (a denial of service) |

The fuzzer did **not** cause a memory crash. My original server already limited each receive to 255 bytes, so the 10,000-character flood couldn't overflow the buffer, and its checks for slashes and `..` already blocked the traversal tests. The two real problems were a logic mistake and an availability problem, not memory corruption.

### How I refactored the C code

1. I added a check for null bytes on the raw received bytes, using the exact byte count. This fixed both null byte failures.
2. I added a 5-second receive timeout on each client socket. This fixed the idle client problem.
3. I replaced the single `recv` with a loop that reads until a newline arrives, with a strict byte limit and my own null terminator for the filename.
4. I replaced the slash and `..` checks with a stricter rule: reject names that are empty, start with a dot, or contain slashes, backslashes, or control characters.
5. I added a check that the file is a regular file, so folders are not served.
6. I moved the per-client work into one function that returns on any error, with one `close` of the client socket in the main loop, so no error path can leave a socket open.

### Results after hardening

All 16 tests passed (0 failures). The two null byte tests now get `ERROR: Invalid filename`, and the idle client is dropped by the timeout so the server keeps serving other clients. Passing these tests only shows the server handled these four kinds of attacks. It doesn't prove the server is safe from everything. Symbolic links and slow-drip clients, listed in the cons above, were not tested.