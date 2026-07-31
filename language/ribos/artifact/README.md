# Ribos bytecode artifact

`artifact/`는 VM ABI 1.0/bytecode ISA 1.0의 canonical `.rba` wire registry,
allocation-free structural reader, codec와 hash를 소유한다. Validated Policy IR
v1.1을 `.rba`로 내리는 emitter implementation은 host compiler 경계인
`language/ribos/host/src/artifact_emitter.c`에 있다.

```text
validated Policy IR + resource closure
  -> canonical little-endian payload
  -> SHA-256 artifact identity
  -> product-bound 232-byte trust message
  -> optional Ed25519 signature envelope
  -> borrowed structural view
```

Target-safe public reader API는 `include/ribos/artifact/`에 있고 host emitter API는
`language/ribos/host/include/ribos/host/artifact_emitter.h`에 있다.

- `host/artifact_emitter.h`: Policy IR validation, resource closure, canonical size
  query와 encoding
- `format.h`: VM/ISA/envelope version, opcode·section registry와 allocation-free reader
- `src/wire.c`: overflow-checked little-endian byte reader/writer
- `src/sha256.c`: payload와 signature-message identity용 SHA-256
- `src/codec.c`: envelope, hash, version, section range와 padding의 structural validation

Artifact는 C structure image가 아니다. 모든 integer는 explicit little-endian
reader/writer로 직렬화하고, 모든 `offset + length`, `count * row_size`와 alignment
계산은 overflow를 검사한다. Directory는 kind 순서이고 section은 8-byte 정렬,
zero-padding, contiguous canonical layout을 사용한다.

Host gate는 다음과 같다.

```sh
make check-ribos-artifact
```

CLI로 source-map을 포함한 unsigned development artifact를 만들 수 있다.

```sh
build/tools/ribosc --emit-artifact policy.rba policy.rbs
```

이 계층은 Ed25519 signature의 크기와 artifact/product/schema/mode/usage/domain/sequence를
봉인하는 canonical 232-byte trust message를 고정하지만 암호학적 signature verification은
수행하지 않는다. 또한 structural reader 성공은 bytecode type/CFG/helper semantics가
검증되었다는 뜻이 아니다. 독립 hostile-byte two-stage verifier는 `vm/` 계층에 있고
`make check-ribos-verifier`로 검사한다. Stage-2는 exact resource closure까지 소유하며 key
policy와 VM dispatch는 별도 책임이다.
