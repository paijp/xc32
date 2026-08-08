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
| `image` | `ghcr.io/paijp/xc32:v1.42` | pin a different toolchain version here |
| `artifact-name` | `firmware` | empty string disables the artifact upload |

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

## Note on caches

GitHub Actions caches (`actions/cache`) are scoped to a single repository and
cannot be shared across projects, so the container image — not a cache — is
what makes the toolchain reusable elsewhere.
