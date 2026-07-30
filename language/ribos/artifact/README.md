# Ribos bytecode artifact

`artifact/`는 validated Policy IR v1.1을 VM ABI 1.0/bytecode ISA 1.0의
canonical `.rba` 파일로 내리는 host emitter와 allocation-free structural reader를
소유한다.

```text
validated Policy IR + resource closure
  -> canonical little-endian payload
  -> SHA-256 artifact identity
  -> optional Ed25519 signature envelope
  -> borrowed structural view
```

Public API는 `include/ribos/artifact/`에 있다.

- `emitter.h`: Policy IR validation, resource closure, canonical size query와 encoding
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
build/tools/ribos-parse --emit-artifact policy.rba policy.rbs
```

이 계층은 Ed25519 signature의 크기와 canonical signing message를 고정하지만
암호학적 signature verification은 수행하지 않는다. 또한 structural reader 성공은
bytecode type/CFG/helper semantics가 검증되었다는 뜻이 아니다. 독립 hostile-byte
verifier, key policy와 VM dispatch는 `vm/` 계층의 후속 책임이다.
