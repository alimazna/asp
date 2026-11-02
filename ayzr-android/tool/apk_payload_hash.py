#!/usr/bin/env python3
"""Hash the reproducible payload of an APK.

An APK is a ZIP with an APK Signing Block appended before the central
directory. Every byte a build produces -- the local file headers, the
compressed entries, and the central directory -- is deterministic. The one part
that is not is the signature itself: an ECDSA signature embeds a random nonce,
so re-signing the same payload yields different bytes.

This prints a SHA-256 over everything up to the start of the APK Signing Block,
which is the part two builds of the same source must agree on. Use it to compare
builds across machines; compare the full-file SHA-256 only for the same signed
artifact.

Usage: apk_payload_hash.py <apk>
"""
import hashlib
import struct
import sys

SIG_BLOCK_MAGIC = b"APK Sig Block 42"


def payload_hash(path: str) -> tuple[str, int, int]:
    with open(path, "rb") as fh:
        data = fh.read()

    magic_off = data.rfind(SIG_BLOCK_MAGIC)
    if magic_off == -1:
        # No signing block (e.g. an unsigned build): the payload is everything
        # before the End Of Central Directory record.
        eocd = data.rfind(b"PK\x05\x06")
        if eocd == -1:
            raise SystemExit("not a ZIP/APK: no end-of-central-directory record")
        return hashlib.sha256(data[:eocd]).hexdigest(), eocd, len(data)

    # Layout: uint64 block_size | ID-value pairs | uint64 block_size | magic.
    # The trailing size field sits immediately before the magic and counts the
    # bytes after the leading size field (pairs + the two size fields' second
    # copy + the magic).
    block_end = magic_off + len(SIG_BLOCK_MAGIC)
    size = struct.unpack("<Q", data[magic_off - 8 : magic_off])[0]
    block_start = block_end - (size + 8)
    if block_start < 0:
        raise SystemExit("malformed APK signing block")
    return hashlib.sha256(data[:block_start]).hexdigest(), block_start, len(data)


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    digest, payload_len, total = payload_hash(sys.argv[1])
    print(f"{digest}  (payload {payload_len} B of {total} B)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
