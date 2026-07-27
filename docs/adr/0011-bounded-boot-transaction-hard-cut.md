---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-27
code_paths:
  - include/Ribon/boot/plan.h
  - include/Ribon/boot/transfer.h
  - include/Ribon/service/directory.h
  - src/common/boot.c
  - src/environments/
  - products/bootmgr/
  - targets/x86_64-uefi-app/
tests:
  - make check-boot-lifecycle
  - make check-target-builds
hardware:
  - none
supersedes:
  - RibonBootSession prepare-commit-quiesce ABI
---

# ADR: Bounded Boot Transaction으로 lifecycle ABI를 hard cut한다

## 맥락

`RibonBootSession`과 분리된 request/plan 호출은 source ownership, durable commit,
environment closure, failure evidence와 point-of-no-return을 하나의 object로 표현하지
못한다. Caller가 payload를 이미 준비한 경우와 service가 payload를 읽은 경우의 책임도
동일한 prepare 표면에 섞인다.

Generic Ribon은 OS, board, firmware native handle을 lifecycle object에 저장하지 않아야
한다. 반면 boot attempt는 deterministic stage, bounded callback time, durable record,
environment closure를 반드시 연결해야 한다.

## 결정

1. `RibonBootSession`, `RibonBootRequest`, 기존 prepare/commit/quiesce/transfer 함수와
   compatibility alias를 삭제한다.
2. `RibonBootTransaction`은 caller-owned input/output storage와 validated Core/service
   authority를 결합하고 단방향 stage를 소유한다.
3. 실행 순서는 `CAPTURE -> VALIDATE_PRODUCT -> FREEZE_PLATFORM_FACTS -> SELECT_SOURCE ->
   VERIFY_MANIFEST -> LOAD_IMAGE -> PREPARE_PROTOCOL -> COMMIT_ATTEMPT ->
   QUIESCE_ENVIRONMENT -> TRANSFER`로 고정한다.
4. `COMMIT_ATTEMPT`는 persistent metadata write와 storage flush의 성공을 요구한다.
   write/flush/deadline failure는 transfer를 허용하지 않는다.
5. `environment-quiesce`는 typed service authority다. UEFI final-map refresh는 commit 뒤
   handoff만 재생성하며, source 선택과 image load를 재실행하지 않는다.
6. Failure는 `RibonBootFailureReceipt`에 stage, reason, static provider ID와 consumed
   budget으로 기록한다. Native pointer와 mutable service handle은 receipt에 넣지 않는다.
7. Public Boot API hard cut에 따라 SDK ABI를 3으로 올린다. Core ABI 3과 Plugin ABI major
   3은 유지한다.

## 결과

Boot Library는 source reader, persistent metadata, flush, timer, closure authority를
명시적으로 요구한다. Network, signature verification, GPT/FAT discovery, OTA writer,
Parus recovery policy와 resident overseer는 이 transaction에 추가하지 않는다. 해당 기능은
별도 service/policy contract에서 source selection 또는 update generation을 제공한다.

## 실패 규칙

- source reader retry는 product `max_retries`를 넘지 않는다.
- callback deadline 만료는 terminal failure다.
- partial metadata write는 committed attempt가 아니며 flush와 transfer를 호출하지 않는다.
- quiesce failure 뒤 transfer를 호출하지 않는다.
- protocol 또는 source fallback은 transaction 내부에서 자동 선택하지 않는다.
