# Monocypher 4.0.3 provenance

- Upstream project: <https://monocypher.org/>
- Release archive: <https://monocypher.org/download/monocypher-4.0.3.tar.gz>
- Release archive SHA-256:
  `8cc9bc341a66249016db9bd70e9142d8d0aef9945973744b1ac05dbc55d8ee66`
- Upstream version: 4.0.3
- License: dual BSD-2-Clause or CC0; the unmodified upstream `LICENCE.md` is
  stored as `LICENSE.md`.
- Retrieval date: 2026-07-31

The following files are byte-for-byte copies from the release archive. No
Ribon patch is applied inside these files.

| Vendored file | Archive path | SHA-256 |
| --- | --- | --- |
| `monocypher.c` | `src/monocypher.c` | `57eb914fc88136119bd41655cccb8c250048bf54d470540625186f8ab16f64be` |
| `monocypher.h` | `src/monocypher.h` | `c494da712122da7ff679fdcf318a5317e84972b6c950fe9d896212947797facd` |
| `monocypher-ed25519.c` | `src/optional/monocypher-ed25519.c` | `60fce3578fb00b00da96490653d993c4cb427b1e1be38183285c66e04d22cc18` |
| `monocypher-ed25519.h` | `src/optional/monocypher-ed25519.h` | `abc4fad381879f5c29176ebe014b9189956b3dfe0a3e36459b6990bc57212380` |
| `LICENSE.md` | `LICENCE.md` | `5f8360e4c06ddcc584bdb4b210c6af824c4bb301e6a9a521869b6d90795ca4b3` |

The release's upstream `make test` suite was executed before integration and
passed. Ribon additionally compiles the selected verification closure with
`-ffreestanding -fno-builtin` for AMD64, AArch64, and RISC-V64.

Monocypher documents Ed25519 compatibility, signature malleability rejection,
and its constant-time scope in its
[Ed25519 manual](https://monocypher.org/manual/ed25519). Version 4.0.3 includes
the upstream fix for the verification timing issue recorded in the
[bug list](https://monocypher.org/bugs) and
[changelog](https://monocypher.org/changelog). Ribon does not alter the
upstream equation implementation.

Monocypher deliberately accepts some non-canonical point encodings for
protocol compatibility. Ribon's external provider wrapper applies canonical
encoding and low-order point rejection before calling `crypto_ed25519_check`.
Those public-input predicates are adapted from libsodium 1.0.22 ref10; its ISC
source is identified by the `1.0.22-RELEASE` tag at
`src/libsodium/crypto_core/ed25519/ref10/ed25519_ref10.c`. Its ISC license is
retained at `third_party/libsodium/1.0.22-strict-filter/LICENSE` with SHA-256
`508a76d186356c0dd807a670ef510964f8724557024796a2c426c6c0e19ab683`.
The wrapper, provider ABI, product selection, and target-closure lints are
Ribon code and are not upstream Monocypher claims.

No signing API is exposed by Ribon target code. Upstream translation units do
contain signing functions, but function/data section garbage collection and a
final-image gate require all signer symbols and the test private seed to be
absent from each selected target image.
