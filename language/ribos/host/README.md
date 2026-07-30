# Ribos host toolchain boundary

`host/`는 `.rbs` compiler와 개발 도구가 의존하는 hosted operation을 소유한다.

```text
host allocator / file writer / formatter
        |
        +--> frontend + Policy IR + artifact emitter
        |
        +--> ribosc / ribos-verify
```

`malloc`, `realloc`, `free`, `FILE`, filesystem, process exit와 Pegen invocation은 이
계층과 host test만 사용할 수 있다. Frontend와 IR은 host build product이지만
allocation과 diagnostic output authority를 explicit adapter로 전달받는다.

`tests/check_boundary.py`는 source와 archive symbol/member 경계를 검사하고
`tests/allocator_tests.c`는 injected allocator의 allocation/release size와 zero-live
closure를 검사한다.

`schema`, artifact structural reader, verifier와 VM runtime은 이 directory를 include
또는 link하지 않는다. Ribon target에서 VM callback을 제공하는 주체는 development
host와 구분해 `embedder`라고 부른다.

Host ABI, pointer와 C structure layout은 `.rba`, product schema identity 또는 VM value
ABI에 포함되지 않는다.
