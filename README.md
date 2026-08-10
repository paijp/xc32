# xc32
- Dockerfile and sample makefile for Microchip XC32 compiler.

- `cd /root&& make target.hex` to build a project.

## Container image

The toolchain is published as a public image on GitHub Container Registry:

```
ghcr.io/paijp/xc32:v1.42   (also tagged :latest)
```

Because it is public, no login or token is needed to pull it — from any
repository, from forks, and from your own machine:

```sh
docker run --rm -v "$PWD:/work" -w /work ghcr.io/paijp/xc32:v1.42 make test.hex
```

## Using it from another project

### Recommended: call the reusable workflow

Nothing about the toolchain has to be repeated in the calling repository:

```yaml
jobs:
  firmware:
    uses: paijp/xc32/.github/workflows/build.yml@main
    with:
      device: 32MX270F256B
      targets: main.hex
```

The project's own `makefile` is used, and the resulting `*.hex` / `*.map` are
uploaded as a build artifact. Inputs:

| input | default | description |
| --- | --- | --- |
| `device` | `32MX270F256B` | passed to `make DEVICE=` / `-mprocessor` |
| `targets` | `test.hex` | space separated make targets |
| `makefile` | *(empty)* | set to `shared` to use this repository's makefile instead of the caller's |
| `working-directory` | `.` | directory to run `make` in |
| `output-directory` | *(empty)* | collect the built `*.hex` / `*.map` into this directory, e.g. `release` |
| `image` | `ghcr.io/paijp/xc32:v1.42` | pin a different toolchain version here |
| `artifact-name` | `firmware` | empty string disables the artifact upload |

### Collecting the output into `release/`

```yaml
jobs:
  firmware:
    uses: paijp/xc32/.github/workflows/build.yml@main
    with:
      targets: main.hex
      output-directory: release
```

The built files are gathered into `release/` and uploaded as the artifact.
Note that this directory lives in the workflow's workspace only — it is not
committed back to the repository.

To attach the same files to a GitHub Release, add a job in the calling
repository that downloads the artifact:

```yaml
  release:
    needs: firmware
    if: startsWith(github.ref, 'refs/tags/')
    runs-on: ubuntu-latest
    permissions:
      contents: write
    steps:
      - uses: actions/download-artifact@v4
        with:
          name: firmware
          path: release
      - uses: softprops/action-gh-release@v2
        with:
          files: release/*
```

### Alternative: use the image directly

```yaml
jobs:
  build:
    runs-on: ubuntu-latest
    container: ghcr.io/paijp/xc32:v1.42
    steps:
      - uses: actions/checkout@v4
      - run: make DEVICE=32MX270F256B main.hex
```

The image sets `PATH` to include the toolchain, and exports `XC32BIN` and
`XC32VER`. The bundled `makefile` honours `XC32BIN`, `XC32VER` and `DEVICE`
as overridable variables.

## Why the installer has to be stopped

The XC32 v1.x installer does its job correctly — it writes `Installation
completed` to `/tmp/bitrock_installer.log` and verifies all 12488 installed
files — and then never exits. A plain `RUN ./xc32-...run` hangs the image
build forever.

Inspecting the stuck process shows what happens. The main thread sits in
`pthread_cond_destroy` (confirmed by resolving its return address against
libc), waiting on the `__wrefs` field of a condition variable. A worker
thread of the installer's thread pool is still parked on `__g_signals` of
that same condition variable, and nothing ever signals it:

```
__g_refs[1]    = 2     one waiter registered
__wrefs        = 12    = (1 waiter << 3) | destroy-pending flag
```

Destroying a condition variable that still has waiters is undefined
behaviour under POSIX, and since glibc 2.25 the implementation blocks until
every waiter has left. That is why the same binary used to install fine:
nothing about the installer changed, the base image's glibc did. Measured
with one installer across three bases:

| base | glibc | installer |
| --- | --- | --- |
| `debian:stretch-slim` | 2.24 | exits 0 after 54 s |
| `debian:buster-slim` | 2.28 | hangs |
| `debian:bookworm-slim` | 2.36 | hangs |

Simply using an old base is not an option here: GitHub Actions runs JS
actions such as `actions/checkout` with node20 *inside* the job container,
and node20 needs glibc ≥ 2.28 — so no glibc both avoids the hang and runs
the runner's node.

Giving the installer an old glibc of its own does not work either. The
`.run` is statically linked with no `PT_INTERP`, so there is nothing to
redirect with an explicit loader, and the process that actually deadlocks is
a dynamically linked child that the stub launches itself — it always gets
the system loader. Pointing that child at an old libc through
`LD_LIBRARY_PATH` fails immediately, because a loader and its libc have to
come from the same glibc build:

```
Inconsistency detected by ld.so: dl-call-libc-early-init.c: 37:
_dl_call_libc_early_init: Assertion `sym != NULL' failed!
```

So the install is left to finish and the installer is then stopped: the
Dockerfile waits for the installer's own `Installation completed` marker,
terminates the process, and verifies the result by running `xc32-gcc
--version`. Everything the build does is a shell command visible in the
Dockerfile — no compiled helper is introduced, and nothing built elsewhere
is copied into the image, so what the image contains is exactly what the
vendor installer produced.

This is needed only for the v1.x installers. From v2.50 onwards the
installer exits on its own, so there the whole loop reduces to a plain
`RUN ./installer.run ...`.

A preloaded stub returning immediately from `pthread_cond_destroy` also
works and lets the installer exit on its own, but it puts a locally built
binary into a published layer, which makes the image harder for anyone else
to audit. Since v1.42 is frozen and the install runs once, that trade is not
worth it here.

## Keeping the installer as evidence

The installer is downloaded in a layer of its own and then kept. A layer is
sealed once the instruction that produced it finishes, so that layer holds
the exact bytes that were about to be executed, and nothing the installer
does afterwards can reach them. If a tampered installer ever came down that
URL and erased itself while running, the sample would still be recoverable:

```sh
docker save ghcr.io/paijp/xc32:v1.42 | tar -x -C dir
tar -xOf dir/<layer>.tar tmp/installer.run | sha256sum
```

Verified by overwriting the file in the following layer — the running image
shows the replacement, while the earlier layer still yields the original
61213753 bytes with a matching digest. A checksum alone would only say that
something differs; the layer preserves the thing itself, so it can actually
be examined.

The build also prints the digest, so a normal build records what it fetched:

```
49d8d445f83b33934beeba50eda08521d0832341a91e910fc98b05ce284eba76  installer.run
```

This costs 58 MiB of the compressed image. The file is not deleted later:
once it is in a layer, removing it saves nothing on the wire and only makes
it harder to get at.

## XC32 versions

Measured by building one image per version (podman, `debian:bookworm-slim`
base, gzip -1 for the compressed figures):

| | v1.42 | v1.44 | v2.50 | v3.01 | v5.00 |
| --- | --- | --- | --- | --- | --- |
| installer | ELF32 | ELF32 | ELF64 | ELF64 | ELF64 |
| needs i386 libs | yes | yes | no | no | no |
| installer exits on its own | **no** | **no** | yes | yes | yes |
| shim required | **yes** | **yes** | no | no | no |
| download | 58 MiB | 206 MiB | 392 MiB | 370 MiB | 1.05 GiB |
| image | 3.18 GiB | 3.07 GiB | 6.25 GiB | 4.05 GiB | 7.05 GiB |
| compressed | **439 MiB** | 544 MiB | 1.24 GiB | 865 MiB | 2.00 GiB |
| files | 12500 | 11787 | 22156 | 23346 | 7579 |
| gcc | 4.8.3 | 4.8.3 | 4.8.3 | 4.8.3 | 13.2.1 |
| compiler driver | `xc32-gcc` | `xc32-gcc` | `pic32m-gcc` | `pic32m-gcc` | `pic32m-gcc` |

The compressed row is the one that matters for CI: it is what every workflow
run pulls over the network and what the registry stores.

**v1.42 is published because it is the smallest**, and it stays that way
without any pruning of the installed tree. Its image pulls in about 25 s on a
GitHub-hosted runner.

The jump from v1.44 to v2.50 is where the size roughly doubles. It is not
bloat in the PIC32MX toolchain — v2.50 is the version that added ARM
Cortex-M support, and that support is simply a second toolchain living
alongside the first:

```
v1.44   pic32mx 2317 MB
v2.50   pic32mx 2619 MB   pic32c 2401 MB
v3.01   pic32mx 2030 MB   pic32c 1018 MB
v5.00   pic32m   368 MB   pic32c 5624 MB
```

So for a PIC32MX project, everything from v2.50 onwards ships a large amount
of material that will never be used. It could be deleted after installing,
but that means shipping a doctored toolchain rather than what the vendor
installs, so this repository does not do it: the image contains exactly what
the installer produces.

Versions other than v1.42 are not published. To build one locally:

```sh
docker build --build-arg XC32VER=v3.01 -t xc32:v3.01 .
```

Note that v2.50 and later use a different download URL path and do not need
the i386 packages, so the Dockerfile needs adjusting for them.

## Note on caches

GitHub Actions caches (`actions/cache`) are scoped to a single repository and
cannot be shared across projects, so the container image — not a cache — is
what makes the toolchain reusable elsewhere.
