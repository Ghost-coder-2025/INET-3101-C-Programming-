#!/usr/bin/env python3
"""
fuzzer.py - generates valid and corrupted flight_data.dat test files for the
Colossus Airlines C program.

File layout (little-endian):
    48 records of 108 bytes each = 5184 bytes
    records  0-23 = outbound seats 1-24
    records 24-47 = inbound  seats 1-24

Each record:
    offset   0: int  seatID        (4 bytes)
    offset   4: int  assigned      (4 bytes)
    offset   8: char lastname[50]
    offset  58: char firstname[50]
"""

import os
import struct

RECORD_SIZE = 108
NUM_RECORDS = 48
FILE_SIZE = RECORD_SIZE * NUM_RECORDS  # 5184
NAME_SIZE = 50


def make_record(seat_id, assigned=0, last=b"", first=b""):
    """Build one 108-byte record. Names are padded with null bytes."""
    last = last[:NAME_SIZE].ljust(NAME_SIZE, b"\x00")
    first = first[:NAME_SIZE].ljust(NAME_SIZE, b"\x00")
    return struct.pack("<ii", seat_id, assigned) + last + first


def make_valid():
    """48 valid records: outbound seat 5 and inbound seat 7 are assigned."""
    records = []
    for flight in range(2):                 # 0 = outbound, 1 = inbound
        for seat in range(1, 25):
            if flight == 0 and seat == 5:
                records.append(make_record(seat, 1, b"Smith", b"Ann"))
            elif flight == 1 and seat == 7:
                records.append(make_record(seat, 1, b"Jones", b"Bob"))
            else:
                records.append(make_record(seat))
    data = b"".join(records)
    assert len(data) == FILE_SIZE
    return data


def record_offset(flight, seat):
    """Byte offset where a seat's record starts (flight 0/1, seat 1-24)."""
    return (flight * 24 + (seat - 1)) * RECORD_SIZE


def write_file(name, data):
    with open(name, "wb") as f:
        f.write(data)
    print(f"created {name:28s} {len(data):5d} bytes")


def main():
    valid = make_valid()

    # 1. valid.dat - a correct file, used to confirm normal loading works.
    write_file("valid.dat", valid)

    # 2. empty.dat - 0 bytes; tests loading a file with no data at all.
    write_file("empty.dat", b"")

    # 3. truncated_10.dat - ends after 10 seats (1080 bytes); tests a file
    #    that stops partway through the outbound flight.
    write_file("truncated_10.dat", valid[:10 * RECORD_SIZE])

    # 4. truncated_100.dat - only 100 bytes; ends in the middle of a record.
    write_file("truncated_100.dat", valid[:100])

    # 5. oversized.dat - valid data plus 50 extra bytes at the end; tests
    #    that the loader notices unexpected trailing data.
    write_file("oversized.dat", valid + b"X" * 50)

    # 6. garbage_names.dat - non-printable bytes injected into the last name
    #    of outbound seat 5 ("Smith"): S, m, \x01, \xff, \x80, \x00, \x7f...
    #    The name still has a terminator, but contains binary junk.
    data = bytearray(valid)
    off = record_offset(0, 5) + 8           # start of lastname
    data[off:off + 8] = b"Sm\x01\xff\x80\x00\x7f\x02"
    write_file("garbage_names.dat", bytes(data))

    # 7. no_terminator.dat - outbound seat 5 has a 50-letter last name with
    #    no null byte, so the string never ends inside its array.
    data = bytearray(valid)
    off = record_offset(0, 5) + 8
    data[off:off + NAME_SIZE] = b"A" * NAME_SIZE
    write_file("no_terminator.dat", bytes(data))

    # 8. bad_seatid_high.dat - seatID of 99 (out of range 1-24) on the first
    #    outbound record.
    data = bytearray(valid)
    data[0:4] = struct.pack("<i", 99)
    write_file("bad_seatid_high.dat", bytes(data))

    # 9. bad_seatid_negative.dat - seatID of -5 on the first outbound record.
    data = bytearray(valid)
    data[0:4] = struct.pack("<i", -5)
    write_file("bad_seatid_negative.dat", bytes(data))

    # 10. bad_assigned.dat - assigned flag of 7 (only 0 or 1 is valid) on the
    #     first outbound record.
    data = bytearray(valid)
    data[4:8] = struct.pack("<i", 7)
    write_file("bad_assigned.dat", bytes(data))

    # 11. bad_assigned_negative.dat - assigned flag of -1 on the first
    #     outbound record.
    data = bytearray(valid)
    data[4:8] = struct.pack("<i", -1)
    write_file("bad_assigned_negative.dat", bytes(data))

    # 12. random_garbage.dat - 5184 random bytes; correct size, random content.
    write_file("random_garbage.dat", os.urandom(FILE_SIZE))

    # 13. all_zeros.dat - 5184 zero bytes; correct size, but every seatID is 0.
    write_file("all_zeros.dat", b"\x00" * FILE_SIZE)

    # 14. all_ff.dat - 5184 bytes of 0xFF; every integer is -1, names are
    #     non-printable and never terminated.
    write_file("all_ff.dat", b"\xff" * FILE_SIZE)


if __name__ == "__main__":
    main()