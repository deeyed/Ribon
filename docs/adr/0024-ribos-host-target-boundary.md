---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/base/
  - language/ribos/host/
  - language/ribos/frontend/
  - language/ribos/ir/
  - language/ribos/artifact/
  - language/ribos/vm/
tests:
  - make check-ribos-host-boundary
  - make check-ribos-parser-pilot
  - make check-ribos-verifier
  - make check
  - make docs
hardware:
  - none
supersedes:
  - implicit hosted dependencies in Ribos compiler and target verifier builds
---

# ADR 0024: Ribos host와 target object graph hard cut

## Context

Ribos frontend, Policy IR, artifact emitter와 verifier가 하나의 host executable로
검증되었더라도 그 executable의 성공은 firmware에서 실행 가능한 VM 경계를
증명하지 않는다. 기존 소스 배치는 parser CLI, Pegen 도구, emitter와 verifier CLI를
각 의미 계층 아래에 두었고 frontend/IR storage가 C allocation과 `FILE` output을
직접 선택했다. 이 상태에서는 다음 위험이 있었다.

- target product가 parser, Pegen 또는 artifact emitter를 우발적으로 링크할 수 있다.
- verifier가 allocation-free여도 같은 빌드 묶음의 hosted libc 의존과 분리되지 않는다.
- frontend/IR API가 allocator와 diagnostic sink의 authority를 숨긴다.
- host 테스트 성공을 architecture-neutral runtime 준비 증거로 오인할 수 있다.

## Decision

Ribos를 source semantics가 아니라 object graph 기준으로 hard cut한다.

```text
base neutral contracts
  + schema + artifact reader + verifier
        -> libribos-target-core.a

host libc adapters
        -> libribos-host-support.a

frontend + Policy IR + artifact emitter
        -> libribos-host-compiler.a
```

- `language/ribos/base`는 allocator와 writer descriptor 및 allocation-free checked
  size/offset 산술만 소유한다. libc allocator, `FILE`, filesystem 또는 process API를
  포함하지 않는다.
- `language/ribos/host`는 hosted allocator/writer/format adapter, parser/verifier CLI,
  artifact emitter와 explicit Pegen integration을 소유한다.
- Frontend와 Policy IR의 동적 storage는 caller가 전달한 `RibosAllocator` authority만
  사용한다. Diagnostic dump는 caller가 전달한 `RibosWriter`만 사용한다.
- Artifact emitter는 host compiler archive에만 들어간다. Artifact reader, wire codec,
  SHA-256, product schema와 independent verifier만 target core archive에 들어간다.
- 정상 build는 tracked parser snapshot을 컴파일하며 Pegen을 호출하지 않는다.
- Target archive는 freestanding flags로 컴파일하고 hosted allocator/I/O symbol,
  frontend, Policy IR와 emitter member 유입을 hard gate로 거부한다.

이 결정은 VM dispatch를 구현하지 않는다. `libribos-target-core.a`라는 이름은
architecture-neutral verifier 기반을 뜻하며 Ribon boot product에 VM이 통합되었다는
뜻이 아니다.

## Consequences

- Host compiler는 architecture와 boot platform을 알지 않으며 `.rbs`에서 `.rba`까지의
  변환만 수행한다.
- 향후 target VM은 parser나 IR을 링크하지 않고 verified artifact view와
  product-generated helper table만 소비할 수 있다.
- Allocator identity가 IR module lifetime에 결속되어 서로 다른 allocator authority의
  object를 섞는 호출은 거부된다.
- Firmware allocator, bounded arena 또는 test fault injector를 ABI 변경 없이 주입할 수
  있다.
- `make check-ribos-host-boundary`는 소스 scan, archive membership과 undefined symbol
  scan을 함께 수행한다.
- 이 gate는 VM interpreter, signature trust, rollback, QEMU 또는 hardware execution을
  증명하지 않는다.

## 기각한 대안

### 하나의 `libribos.a`와 compile-time define

잘못된 define이나 transitive dependency로 host compiler가 target에 유입되어도 link
graph에서 드러나지 않으므로 기각한다.

### Frontend가 `malloc`과 `FILE`을 계속 직접 사용

Hosted tool에서는 간단하지만 resource authority와 test fault injection을 숨기고 target
reuse 경계를 흐리므로 기각한다.

### Artifact emitter를 target core에 포함

Boot runtime은 untrusted artifact를 읽고 검증해야 하며 Policy IR을 serialize할 이유가
없다. 공격 표면과 code size만 늘리므로 기각한다.
