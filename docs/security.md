# Security

[Documentation](readme.md) · [Project overview](../readme.md)

## Surface

Registry metadata and package build instructions are external inputs. Installation
runs scripts from the configured registry with the invoking user's privileges.

## Protections

Self-update accepts HTTPS metadata and downloads, verifies the published checksum
and version, and replaces the executable only after validation. Registry sync
uses Git's fast-forward checks and rejects dirty or unrelated checkouts.

Resolution validates registry JSON, owns metadata strings, and handles HTTP errors.
C++ resolution exceptions are converted to Tcl errors. Preview skips installation
and telemetry; tests use disposable projects. Dependency builds use version-pinned
HTTPS downloads, and the solver uses a pinned release.

## Pending

Registry authentication, signed package manifests, resource limits, and downloaded
archive checksum verification are not added in this migration. The existing range
and prerelease semantics are preserved rather than replaced with npm semantics.
Large-registry performance and non-Linux platform validation remain follow-up work.
