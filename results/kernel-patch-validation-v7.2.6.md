# Kernel patch series validation — Linux 7.2.6

Validation date: 2026-10-01

## Base kernel

Upstream base commit:

    500df175a7f9e6bc1a9c328590ca5150f84f9ff0

Commit subject:

    Linux 7.2.6

## Patch series

The Orange Pi 4A / Allwinner A523/T527 / IMX219 kernel work is represented by:

    0001 ... 0022

All 22 patches were applied to the clean Linux 7.2.6 base using `git am`.

Result:

    22/22 patches applied successfully

No patch conflicts occurred.

## Aggregate diff verification

After applying all 22 patches, the resulting tree diff was generated with:

    git diff 500df175a7f9e6bc1a9c328590ca5150f84f9ff0..HEAD

The generated diff was compared byte-for-byte with:

    0000-full-series-v7.2.6.patch

Result:

    EXACT MATCH: 0001-0022 == 0000 aggregate diff

Therefore `0000-full-series-v7.2.6.patch` is an exact aggregate representation
of the complete 22-patch series.

## Integrity

`SHA256SUMS` contains checksums for:

- `0000-full-series-v7.2.6.patch`
- `0001` through `0022`

Total:

    23 files

The manifest was verified using:

    sha256sum -c SHA256SUMS

All entries passed verification.

## Author metadata

Public patch metadata uses:

    Nick Sl <335926735+nickslkr@users.noreply.github.com>

The private Gmail address used during development was removed from the
public patch set before publication.
