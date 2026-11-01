# Security

Do not put account tokens or private messages in public issues. For a security
defect, use [GitHub private vulnerability reporting](https://github.com/Cavanshirpro/discord-nova/security/advisories/new)
when enabled, or contact the maintainer privately through their GitHub profile.
Include affected release, platform and a minimal reproduction with fake data.

The client keeps credentials in process memory and does not persist sessions.
It does not offer plugins in v0.1. Remote messages are rendered as plain text
and terminal control bytes are removed. The unofficial API can require account
verification; the client does not bypass it.
