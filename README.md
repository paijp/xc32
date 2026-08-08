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

## Note on the installer

The XC32 v1.42 installer completes its work correctly — it writes
`Installation completed` to `/tmp/bitrock_installer.log` and verifies all
12488 installed files — but then never exits, deadlocking in a futex wait.
A plain `RUN ./xc32-...run` therefore hangs the image build forever. The
Dockerfile works around this by running the installer in the background,
waiting for the completion marker in its log, terminating it, and then
verifying the toolchain by running `xc32-gcc --version`.

## Note on caches

GitHub Actions caches (`actions/cache`) are scoped to a single repository and
cannot be shared across projects, so the container image — not a cache — is
what makes the toolchain reusable elsewhere.
