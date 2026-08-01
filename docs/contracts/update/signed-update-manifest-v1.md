---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - include/Ribon/update/manifest.h
  - src/update/manifest.c
  - tools/update_manifest.py
tests:
  - make check-update-manifest
  - make check-update-manifest-sanitizer
  - make check-update-manifest-cross-compile
  - update_manifest_tests
hardware:
  - none
supersedes:
  - implicit JSON update metadata
---

# Signed update manifest v1 계약

## 목적과 신뢰 경계

Update manifest는 transport와 storage address를 실행 authority로 사용하지 않는다. 한 manifest는
bounded component byte identity와 product, target, protocol, mode, rollback domain 및 sequence를
canonical bytes로 결속한다. Detached signature envelope는 이 exact manifest에 대한
`UPDATE_MANIFEST` single key usage만 표현한다.

```text
source-neutral host component manifest
  -> exact component read와 SHA-256 확인
  -> canonical manifest bytes
  -> update-only signed message
  -> offline signature
  -> detached signature envelope
  -> independent target reader
  -> product binding
  -> immutable key policy
  -> selected Ed25519 provider
```

Manifest codec와 authorizer는 network fetch, storage write, slot state transition 또는 protected
rollback floor I/O를 수행하지 않는다. Codec 성공은 component payload가 설치 또는 readback 단계에서
검증되었다는 뜻이 아니다.

## 권한 표

| 입력 또는 결정 | 소유자 | Manifest authorizer의 의무 |
| --- | --- | --- |
| source path와 component exact read | host assembler | target wire에 path를 넣지 않음 |
| product와 target tuple | source product graph | exact digest와 protocol version 비교 |
| key ID와 public key | immutable key-policy store | unique record와 usage/domain/sequence 승인 |
| private key | offline signer | target ABI와 image에 포함하지 않음 |
| signature equation | selected Ed25519 provider | canonical 256-byte message만 검증 |
| rollback floor와 trial | protected-state/update policy | 이 codec 밖에서 별도 승인 |
| payload write와 readback | selected storage/update provider | manifest 검증 뒤에도 content digest 재검증 |

## Bounded registry

Manifest는 최대 16개 component를 가진다. Component role은 OS 이름이 아닌 semantic class다.

| Value | Role | Destination class |
| ---: | --- | --- |
| 1 | kernel | kernel slot |
| 2 | boot module | module slot |
| 3 | policy | policy slot |
| 4 | firmware | firmware slot |
| 5 | recovery image | recovery slot |

Kernel, policy와 recovery image role은 각각 singleton이다. Boot module과 firmware는 반복할 수
있다. Role과 destination class의 다른 조합, unknown value와 duplicate singleton은 거부한다.

Image-format registry는 `OPAQUE=1`, `ELF64=2`, `PE_COFF=3`, `LINUX_IMAGE=4`, `RAW=5`다.
`REQUIRED=bit 0` 이외의 component flag는 v1에서 거부한다. Execution mode와 key usage는
{doc}`../security/signed-object-trust-v1`의 stable registry를 사용한다. Signature message와
envelope의 usage는 항상 `UPDATE_MANIFEST=5`다.

## Canonical manifest wire

모든 integer는 unsigned little-endian이다. Packed C struct, pointer, `size_t`, native enum layout,
JSON text, source path와 padding은 wire identity에 포함하지 않는다.

### 256-byte header

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 32 | zero-padded ASCII `RIBON-UPDATE-MANIFEST-V1` |
| 32 | 2 | major = 1 |
| 34 | 2 | minor = 0 |
| 36 | 4 | header bytes = 256 |
| 40 | 8 | exact total bytes |
| 48 | 4 | section count = 2 |
| 52 | 4 | component count, 1..16 |
| 56 | 8 | positive bundle generation |
| 64 | 8 | predecessor generation, smaller than generation |
| 72 | 8 | rollback sequence |
| 80 | 8 | creation-policy version |
| 88 | 2 | hash algorithm = SHA-256 |
| 90 | 2 | signature algorithm = Ed25519 |
| 92 | 2 | execution mode |
| 94 | 2 | flags = 0 |
| 96 | 32 | manifest schema digest |
| 128 | 128 | four-entry section directory |

Section-directory entry는 32 byte며 `type:u32`, `flags:u32`, `offset:u64`, `length:u64`,
`count:u32`, `entry_size:u32` 순서다. 첫 entry는 offset 256의 exact 256-byte binding section,
두 번째는 offset 512의 component table이다. 나머지 두 entry는 모두 0이다. Section 순서,
offset, length, count와 entry size가 canonical value와 다르면 거부한다.

### 256-byte binding section

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 32 | exact product identity digest |
| 32 | 32 | architecture identity digest |
| 64 | 32 | platform identity digest |
| 96 | 32 | entry environment identity digest |
| 128 | 32 | Boot Protocol identity digest |
| 160 | 32 | rollback-domain digest |
| 192 | 2 | Boot Protocol major |
| 194 | 2 | Boot Protocol minor |
| 196 | 4 | minimum hardware revision |
| 200 | 4 | maximum hardware revision |
| 204 | 52 | reserved = 0 |

모든 identity digest는 nonzero SHA-256이다. Product graph는 stable ID의 exact UTF-8 bytes 또는
대응 계약이 정의한 source identity를 hash한다. Prefix, case folding, filename과 runtime board
추론을 사용하지 않는다. Hardware revision은 inclusive range이며 minimum이 maximum보다 클 수 없다.

### 192-byte component entry

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 32 | stable logical-ID digest |
| 32 | 32 | exact payload content digest |
| 64 | 32 | semantic destination-ID digest |
| 96 | 32 | entry-contract digest |
| 128 | 8 | payload offset in component bundle |
| 136 | 8 | exact payload bytes, nonzero |
| 144 | 8 | maximum payload bytes |
| 152 | 2 | component role |
| 154 | 2 | destination class |
| 156 | 2 | image format |
| 158 | 2 | component flags |
| 160 | 4 | install order |
| 164 | 28 | reserved = 0 |

Table order가 install order다. 첫 row는 0이고 각 row는 이전 값의 exact successor다. Logical ID는
중복될 수 없다. `offset + exact_size`는 wrap하지 않아야 하며 component half-open range는 서로
겹치지 않는다. `exact_size <= maximum_size`를 만족해야 한다.

## Canonical 256-byte signed message

Signature input은 manifest bytes를 직접 서명하는 대신 다음 bounded message를 서명한다. Manifest
digest가 component table과 target binding 전체를 봉인하며 direct product/schema/domain field가
object-class 혼동을 막는다.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 32 | zero-padded ASCII `RIBON-UPDATE-MESSAGE-V1` |
| 32 | 2 | message major = 1 |
| 34 | 2 | message minor = 0 |
| 36 | 2 | manifest major = 1 |
| 38 | 2 | manifest minor = 0 |
| 40 | 2 | hash = SHA-256 |
| 42 | 2 | signature = Ed25519 |
| 44 | 2 | execution mode |
| 46 | 2 | key usage = `UPDATE_MANIFEST` |
| 48 | 8 | flags와 reserved = 0 |
| 56 | 8 | rollback sequence |
| 64 | 8 | exact manifest bytes |
| 72 | 8 | bundle generation |
| 80 | 8 | predecessor generation |
| 88 | 8 | creation-policy version |
| 96 | 32 | SHA-256(manifest exact bytes) |
| 128 | 32 | product digest |
| 160 | 32 | manifest schema digest |
| 192 | 32 | rollback-domain digest |
| 224 | 32 | SHA-256(key ID exact bytes) |

Ribos policy trust message와 update signed message는 domain separator와 layout이 다르다. Policy key
usage로 update manifest를 승인하거나 update usage로 Ribos policy를 승인하는 compatibility path는
두지 않는다.

## Detached signature envelope

Envelope header는 exact 160 byte다. 뒤에는 padding 없이 1..64 byte key ID와 exact 64-byte
Ed25519 signature가 이어진다.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 32 | zero-padded ASCII `RIBON-UPDATE-SIGNATURE-V1` |
| 32 | 2 | major = 1 |
| 34 | 2 | minor = 0 |
| 36 | 4 | header bytes = 160 |
| 40 | 8 | exact envelope bytes |
| 48 | 8 | exact manifest bytes |
| 56 | 2 | hash = SHA-256 |
| 58 | 2 | signature = Ed25519 |
| 60 | 2 | key usage = `UPDATE_MANIFEST` |
| 62 | 2 | execution mode |
| 64 | 2 | key-ID bytes |
| 66 | 2 | signature bytes = 64 |
| 68 | 4 | flags = 0 |
| 72 | 8 | key-ID offset = 160 |
| 80 | 8 | signature offset = 160 + key-ID bytes |
| 88 | 32 | manifest digest |
| 120 | 32 | signed-message digest |
| 152 | 8 | reserved = 0 |

Envelope reader 성공은 cryptographic success가 아니다. Authorizer는 manifest와 message digest를
재계산하고 product expectation을 비교한 뒤 key-policy request를
`mode + UPDATE_MANIFEST + product + rollback-domain + sequence`로 구성한다. Key-policy 승인 뒤
selected Ed25519 provider만 signature equation을 검사한다.

## Host source manifest와 tool 경계

`tools/update_manifest.py`의 JSON input은 host-side source manifest이며 target wire가 아니다. Tool은
component를 exact read하고 declared SHA-256, nonzero size와 maximum size를 검사한다. Stable logical
name, product, architecture, platform, environment, protocol, destination와 entry-contract UTF-8 bytes는
SHA-256 identity로 내린다. 같은 source bytes와 metadata는 byte-identical manifest를 만든다.

Tool은 다음 surface를 제공한다.

- `assemble`: source manifest를 canonical binary manifest로 생성
- `inspect`: binary manifest를 수정하지 않고 independent validation
- `message`: offline signer가 소비할 exact 256-byte message 생성
- `envelope`: caller가 제공한 detached signature를 envelope로 조립
- `inspect-envelope`: signature를 승인하지 않고 envelope shape와 digest 표시

Private key와 signer process는 tool의 manifest codec이나 target library에 포함되지 않는다.

## Fail-closed ordering

Target authorizer는 다음 순서를 지킨다.

1. manifest magic, version, exact total과 section directory 검사
2. binding identity, reserved, component row, singleton, range와 upper bound 재유도
3. selected product expectation과 mode/protocol/hardware tuple 비교
4. detached envelope의 version, offsets, size, usage와 reserved 검사
5. manifest digest와 signed-message digest 재계산
6. immutable key policy에서 exact key, product, mode, update usage, domain과 sequence 승인
7. exact 256-byte message에 selected Ed25519 provider 실행
8. 성공한 pointer-free key decision과 borrowed manifest view 반환

첫 실패가 stable `RibonUpdateManifestStatus`를 소유한다. 실패한 output view와 decision은 실행 또는
storage authority로 사용할 수 없다.

## 증거와 비목표

Canonical vector는 896-byte two-component manifest, 256-byte signed message와 251-byte envelope를
C codec, independent Python codec와 OpenSSL Ed25519 경로에서 교차 검사한다. Hostile corpus는
truncation, trailing byte, section range, unknown role/flag, reserved, duplicate identity/singleton,
zero/wrapping/overlapping component와 product/domain/sequence/signature mutation을 거부한다. Target
codec source는 freestanding flags로 x86_64, AArch64와 RISC-V 64 object까지 교차 컴파일한다.

이 계약과 unit evidence는 storage writer, A/B metadata, network fetch, protected rollback floor,
physical flash durability, TPM/RPMB, production private-key custody, RPi5 또는 fleet OTA를 증명하지
않는다. Component content digest는 설치와 full readback 단계에서 다시 검증해야 한다.
