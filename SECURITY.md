# Security and safe publication

## Device safety

Firmware flashing is a potentially destructive operation. Require deliberate activation, explicit hardware/image matching, complete integrity validation, target-identity confirmation and robust acknowledgement checks. Never flash an unidentified or unsupported device.

## Privacy review before publication

Check every tracked file and Git history for:

- IP and MAC addresses, hostnames, usernames, home domains and internal paths;
- real device UIDs, serial numbers, tokens, credentials and keys;
- CAN captures and debug logs that may identify a user's devices;
- firmware binaries and proprietary third-party content.

Do **not** interpret `.gitignore` as a privacy guarantee: ignored files can still be force-added and already-tracked files remain tracked. Review staged changes with `git diff --cached` before each push. If a secret was ever committed, remove it from history and rotate it.

## Reporting

Please avoid posting real device identifiers, credentials or private network details in public issues. Redact captures before sharing them.
