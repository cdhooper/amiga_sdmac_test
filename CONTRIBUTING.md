# Contributing

## Building

The project can be built with the included VS Code dev container or with
Bebbo's gcc Amiga cross-compiler and `amitools` installed locally.

Build all release formats with:

```console
make all lha adf zip
```

To use a specific version, pass it through `VER`:

```console
make all lha adf zip VER=1.2
```

This produces versioned `.lha`, `.adf`, and `.zip` files in the repository
root.

## Releases

Release builds are triggered by pushing a tag whose name starts with
`release_`, for example:

```console
git tag release_1.2
git push origin release_1.2
```

The release workflow builds the versioned archives, creates a draft GitHub
release, and publishes `sdmac.lha` to Aminet under `util/misc`.

Before publishing, configure the repository secret `AMINET_UPLOADER_EMAIL`
with the email address to use as the anonymous Aminet FTP password. The
workflow derives the Aminet package version from the tag and uses
the current UTC date for the package date. It uses [`sdmac.readme`](sdmac.readme)
as its package metadata template.