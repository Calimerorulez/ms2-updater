"""Offline link/fixture check: read host executables as bytes, never run them."""
import hashlib
from pathlib import Path
import sys

production = Path(sys.argv[1]).read_bytes()
recovery = Path(sys.argv[2]).read_bytes()
cases = (
    (0x32, 93500, 252, "2d178f6be8fc82bbea2ff3f75e2e35a37c625d83b6b6671a9637d0d895d62a8c",
     "af26cfdc9f747379f5a7bd7e62df384b641e21c4007dbdf9a19c69b3e808bb69"),
    (0x33, 159219, 0, "48a663111bf4c5b6c1f07d08858070a2a3c97a1d1703785e5b551dfabdf97b6a",
     "cf2d277e38f6f982ffd3b40dd1c372c594cd8b8592fb5234f3b4da6c6ccd2439"),
    (0x34, 126092, 0, "3a80d64f4c6e8f92d5ef49d31e6b8fcacc2b39e575d26949a0e90f1e40c2fc1d",
     "a56765bb56626795f792839272a3621298955c8831cc6ad35e290afa027f33c7"),
)
for hardware, size, offset, synthetic_hash, production_hash in cases:
    data = bytearray((i * 17 + hardware * 3 + i // 251) & 255 for i in range(size))
    data[offset:offset + 4] = bytes((5, 6, 0, hardware))
    if hashlib.sha256(data).hexdigest() != synthetic_hash:
        raise SystemExit(f"Synthetic fixture hash mismatch for {hardware:04X}")
    if production_hash.encode() not in production or synthetic_hash.encode() in production:
        raise SystemExit(f"Production catalog isolation failed for {hardware:04X}")
    if synthetic_hash.encode() not in recovery:
        raise SystemExit(f"Missing recovery test catalog for {hardware:04X}")
    if f"synthetic-{hardware:04x}".encode() in production:
        raise SystemExit(f"Synthetic profile name leaked into production for {hardware:04X}")
print("Catalog isolation: production hashes retained; synthetic hashes test-only; hashlib verified")
