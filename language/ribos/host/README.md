# Ribos host toolchain boundary

`host/`는 `.rbs` compiler와 개발 도구가 의존하는 hosted operation을 소유한다.

```text
host allocator / file writer / formatter
        |
        +--> frontend + Policy IR + artifact emitter
        |
        +--> ribosc / ribos-verify
        |
        +--> ribos-run + replay fixture codec
```

`malloc`, `realloc`, `free`, `FILE`, filesystem, process exit와 Pegen invocation은 이
계층과 host test만 사용할 수 있다. Frontend와 IR은 host build product이지만
allocation과 diagnostic output authority를 explicit adapter로 전달받는다.

`tests/check_boundary.py`는 source와 archive symbol/member 경계를 검사하고
`tests/allocator_tests.c`는 injected allocator의 allocation/release size와 zero-live
closure를 검사한다.

`ribos-run`은 host compiler나 별도 reference interpreter를 링크하지 않는다.
`libribos-target-core.a`의 production verifier, PreparedProgram과 VM execution
entry를 그대로 사용한다. Host가 제공하는 context와 helper callback result는
versioned little-endian `.rbctx`, `.rbtr` fixture에 기록되며 artifact/context/body
SHA-256으로 결박된다.

```sh
build/tools/ribosc --emit-artifact policy.rba policy.rbs
build/tools/ribos-verify policy.rba
build/tools/ribos-run \
    --context context.rbctx \
    --transcript helpers.rbtr \
    policy.rba
```

Replay fixture와 report 계약은
{doc}`../../../docs/contracts/language/ribos-host-replay-v1`이 소유한다.
`tests/replay_tests.py`, `tests/conformance_tests.py`와 `tests/hostile_tests.py`는
동일 입력 반복, ISA 24 opcode, compiler/verifier/runtime resource 일치와 bounded
hostile input을 검사한다.

`schema`, artifact structural reader, verifier와 VM runtime은 이 directory를 include
또는 link하지 않는다. Ribon target에서 VM callback을 제공하는 주체는 development
host와 구분해 `embedder`라고 부른다.

Host ABI, pointer와 C structure layout은 `.rba`, product schema identity 또는 VM value
ABI에 포함되지 않는다.

Host replay는 production signature/rollback, Ribon product service, QEMU 또는
hardware evidence가 아니다.
