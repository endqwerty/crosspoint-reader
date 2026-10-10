# Security

This is a personal fork of CrossPoint Reader for one device, with no releases and no
support (see the [README](README.md)). Report a vulnerability in the fork privately
through GitHub: **Security > Report a vulnerability** on this repository. Problems
that also exist in upstream CrossPoint are better reported to the
[official project](https://github.com/crosspoint-reader/crosspoint-reader/security).

## Known weaknesses

Reviewed 2026-10-10 against upstream `e54c0087`. These come from upstream and are
**not fixed here**, because the fork keeps upstream's design and does not work on the
network features. They matter only if you use those features.

| Area | Weakness |
| --- | --- |
| TLS | Certificates are not verified (`setInsecure()` in `src/network/HttpDownloader.cpp`, `lib/KOReaderSync/KOReaderSyncClient.cpp`, `src/clippings/ClippingSync.cpp`). On a hostile network, OPDS and KOReader credentials can be captured. |
| OTA | The update check downloads over that unverified TLS and checks only the image format, with no publisher signature. Do not use it; it also installs the official firmware, not this fork. |
| Web server, WebDAV, Calibre wireless | No authentication, CORS enabled, no CSRF protection. Anyone on the same network, or on the open AP-mode hotspot, can read, change and delete files and settings while it runs. |
| Protected paths | WebDAV does not call the FAT-alias-aware `protectedpaths::isSensitivePath()`, and `/download` checks only the last path component, so protected files can be reached through short-name aliases or sub-folders. |
| OPDS | Absolute links in a feed receive the configured catalog's username and password, even for another host. |
| Stored passwords | Wi-Fi, OPDS and KOReader passwords on the SD card are Base64 plus XOR with the device MAC address. Treat them as readable by anyone holding the card. |
| EPUB parsing | Extraction trusts the ZIP's declared size, so a crafted EPUB can waste SD space and time before it is rejected. |
| Build | CI workflows use mutable action tags and unpinned installers. GitHub Actions is disabled on this fork. |

**Practical rules for this fork:** keep the web server, AP mode, WebDAV, Calibre and
OPDS off on networks you do not control; do not reuse real passwords for them; load
books by SD card; only open EPUBs you trust.

The fork's own host script `scripts/scan-epub-library.py` bounds how much of each
EPUB entry it reads.

## What the repository must not contain

The repository is public. Do not commit credentials, tokens, private network
addresses, host names or machine paths, or other people's private data. Secret
scanning and push protection are enabled on GitHub. Contributors' names and
public commit emails may appear in attribution notes; do not add other personal
data.
