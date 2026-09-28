# R12 AArch64 firmware 종료와 EL1 인계 결과

## 완료 범위

R12는 AArch64 UEFI 실행 중인 exception level과 stage-1 translation을 실제 레지스터에서
관측하고, `ExitBootServices()`가 성공한 뒤에만 Ribon 소유의 bounded bridge와 stack으로
전환하는 경계를 구현했다. LUCA로 넘기는 표준 RLH1 entry tuple은 live EL1, masked DAIF와
`RIBON_LUCA_ENTRY_FLAG_EL1_NORMALIZED`를 요구한다. Ribon bridge는 LUCA의 영구 pmap이
아니며 kernel entry까지 필요한 code, stack, handoff, payload, DTB, module 및 선택한 port
MMIO만 일시적으로 도달 가능하게 한다.

이 변경은 AArch64 일반 firmware나 모든 EL2 monitor 조합의 지원 선언이 아니다. 실제 guest
증거는 QEMU virt의 EDK2가 EL1에서 시작한 실행 한 건이다. EL2 source path는 host model과
AArch64 PE compile/link로 검증했으며 실제 EL2 firmware 실행은 하지 않았다. UTM과 물리
장비도 실행하지 않았다.

## 원본 선택과 제외

직접 참고한 원본은 `/Users/gungye/workspace/os-analyze/src/freebsd-src`의 revision
`9f5c4ef32812afb4573a278e6eafe5040f839d13`에 있는
`sys/arm64/arm64/locore.S`다. 원본 SHA-256은
`f8455440b5e72fe8430a6ee709f454c7e40cfa0e90a88461af23bb1f54dc32b6`이다.

FreeBSD의 `CurrentEL` 분기, EL2 timer/trap 정규화와 EL1h `eret` 순서를 선택했다. Ribon의
`src/arch/aarch64/transition.S`에는 Andrew Turner의 BSD-2-Clause 원문, FreeBSD revision과
차용 범위를 보존했다. FreeBSD의 VHE stay-at-EL2 정책, FreeBSD pmap·KASAN·SMP bootstrap,
VM page owner와 커널 VA 배치는 가져오지 않았다. Ribon은 LUCA의 EL1 실행 계약에 맞춰
EL2에서 최소 상태만 설정하고 EL1h로 내려간다. Linux와 Zircon 소스·macro·test는 이식하지
않았다.

## 구현 결과

### 진입 상태와 bounded bridge

`include/Ribon/arch/aarch64/entry_state.h`와
`src/arch/aarch64/entry_state.c`는 다음을 소유한다.

- live `CurrentEL`에 따라 EL1 또는 EL2의 현재 register bank만 읽는 entry snapshot
- `AT`/`PAR` 결과로 firmware-active translation에서 pointer가 identity-addressed인지 검사
- page alignment, overflow, ordering, overlap, memory kind와 최대 range 수를 검사하는 range set
- root 한 페이지와 최대 여덟 L2 page를 쓰는 39-bit TTBR0 bridge
- normal memory와 device memory attribute를 분리한 필요한 2 MiB block만의 identity mapping
- source EL, bridge root, target stack, continuation과 context를 묶는 versioned transition plan

기존 AArch64 arch callback이 만들던 낮은 512 GiB 전체 identity map과 firmware 종료 전
stack 전환은 삭제했다. Public `RibonArchOps` ABI는 6으로 올렸으며 더 이상 일반 arch
callback으로 post-exit identity helper를 게시하지 않는다.

### firmware 종료 transaction

`targets/uefi-app/entry.c`는 firmware service가 살아 있는 동안 loaded image, transition code,
bridge table, stack, handoff storage, payload source와 load segment, DTB, boot module 및 port MMIO의
범위를 검증하고 bridge를 준비한다. `ExitBootServices()` 실패 또는 map-key 갱신 재시도에서는
firmware stack과 translation을 그대로 유지한다. 성공 callback만 bridge를 활성화하고 EL1
continuation으로 이동한다. Post-exit payload copy가 필요한 direct-FDT development product는
bridge stack에서 copy와 cache sync를 끝낸 뒤 LUCA로 transfer한다.

UEFI native context는 loaded image base와 size를 보존한다. Port ABI 2는 firmware 종료 뒤 필요한
MMIO base와 size를 게시하며 QEMU PL011 port는 4 KiB device range를 제공한다. 비AArch64 UEFI
build에는 이 AArch64 전용 state와 continuation이 나타나지 않도록 compile-time boundary를 둬
x86_64 fixture를 보존했다.

### LUCA protocol 인계

RLH1 flag bit 5를 `RIBON_LUCA_ENTRY_FLAG_EL1_NORMALIZED`로 정의했다. AArch64 RLH1 producer는
`RLH1 | EL1_NORMALIZED`, privilege EL1과 masked interrupts를 게시한다. AArch64 direct-FDT
development tuple도 argument 1에 같은 normalized flag를 전달한다. 다른 architecture의 RLH1
계약은 기존 current-supervisor 조건을 유지한다.

## R12 작업 대응

| 작업 | 실제 결과 |
| --- | --- |
| W01 | `ribon_aarch64_entry_state_capture()`가 live EL에 맞는 register만 읽고 EL1·EL2 host model이 각 분기를 검사한다. |
| W02 | `ribon_uefi_app_exit_boot_services()` 성공 callback에서만 `uefi_activate_post_exit_bridge()`를 호출한다. 실패·changed-map-key 시험에서 callback 선행 실행이 0회임을 확인했다. |
| W03 | `ribon_aarch64_post_exit_transition()`이 VHE 없이 EL2 timer/trap 상태를 최소화하고 EL1h로 `eret`한다. EL1 continuation은 live EL1을 다시 확인한다. |
| W04 | 최대 32개 입력 range를 정렬·병합하고 최대 9 page table로 필요한 block만 mapping한다. 범위 밖 lookup, 잘못된 정렬·겹침·kind·capacity를 거부한다. |
| W05 | entry-state ABI v1, port ABI 2, RLH1 normalized flag와 post-exit contract 문서를 동결했다. Ribon bridge와 LUCA permanent pmap의 owner를 분리했다. |
| W06 | 이 child 변경은 별도 큐를 제외한 이 commit으로 먼저 고정하며, LUCA parent는 게시된 child SHA만 gitlink로 참조한다. |

## 검증

| 증거 | 명령/입력 | 결과 | 로그 SHA-256 |
| --- | --- | --- | --- |
| V01·V02·V03 host contract | `make -j6 -B check-arch-aarch64 check-arch-ops check-luca-entry-contract check-luca-direct-fdt check-rlh1 check-uefi-exit-transaction check-port-services` | pass. EL1·EL2 model, bounded bridge, malformed range, firmware retry와 protocol tuple을 실행 | `10b8560377340170c96fdccc8b3deddc1a31550e24ccf313beb72943ec1883a9` |
| x86 UEFI 회귀 | `make -j6 -B x86_64-uefi-parus-fixture` | compile/link pass | `9926c9404341f03816a7c4f263c0d160adc9c2efc48ecb2628cb24b3a399c73b` |
| AArch64 PE | `make -j6 -B aarch64-uefi-parus-fixture check-aarch64-uefi-pe` | machine `0xaa64`, EFI application, relocation 확인 | `10cda3db3087a2f128b0ceebf4a57658859da3da035d13db968324949e3b7492` |
| evidence harness unit | `python3 tests/tools/qemu_target_smoke_tests.py` | 25 tests pass | `fc149ee69d996ef467e55ca226c67a076cda86638f7d4794cf81753cf582379c` |
| 결합 guest | `aarch64-uefi-luca-direct-fdt-dev-smoke` + EDK2 + LUCA kernel/world + ext2 data volume | `RIBON-QEMU-EVIDENCE-OK aarch64-uefi`, 5.674 s, timeout 없음 | `32d99bb32a78740fda0678915e4f4f13070da1c0589665c6ebdecbc7e0feb0e5` |

실제 결합 guest에서 다음 marker를 각각 한 번 관측했다.

```text
RIBON-R12-AARCH64-FIRMWARE-SOURCE-EL=0x0000000000000001
RIBON-R12-AARCH64-EL1-BRIDGE-SOURCE-EL=0x0000000000000001
RIBON-R4-UEFI-EXIT-BOOT-SERVICES-OK
LUCA:BM:v0:01000100:LOCORE:ENTER:NONE
LUCA:BM:v0:05000100:KMAIN:ENTER:NONE
LUCA:SYSINIT:v0:ENTRY:OK:external-initial-user-runtime
LUCA:SYSINIT:v0:ENTRY:OK:system-running
LUCA:TERMINAL:v1:INPUT_READY
```

QEMU evidence JSON은 `outcome=passed`, `terminal=required-evidence-observed`,
`first_divergence=null`, `cleanup.complete=true`를 기록했다. EDK2 SHA-256은
`ee769c4bf42a5350d33345fc1b16d00156fe0c25fb3c68debd003bd844e4e3fa`, LUCA payload는
`7600e10e3d85e11624c1def18f3e410205207063dbea543ba6c12d3eff762e58`, World package는
`5826baeab0f07b277dea72ca863d40b3a15253200c859bcbf3830ce1baa83d88`이다. 별도 ext2 data
volume은 전후 모두
`6489b882d8287d22c53c2af6cfe6f75b5121483dda5f4f36773409e8e6bd73c1`로 불변이었다.

## 복구 이력

- `R12-E0001`: host test가 privileged `ID_AA64MMFR0_EL1`을 Darwin process에서 읽으려 한 첫
  구현을 제거했다. 이미 snapshot한 TCR에서 PA range를 유도해 host model과 runtime register
  접근 경계를 분리했다.
- `R12-E0002`: adjacent range 병합 뒤 다음 element 이동이 잘못된 것을 hostile range corpus가
  찾았다. 다음 index를 재검사하도록 고친 뒤 malformed corpus 전체를 다시 통과했다.
- `R12-E0003`: 기존 object가 이전 ABI를 보존해 port contract가 실패했다. 입력이 바뀐 host
  group을 `-B`로 재생성해 ABI 2 source와 object가 일치함을 확인했다.
- `R12-E0004`: x86 UEFI compile이 AArch64 전용 post-exit bridge symbol의 scope를 발견했다.
  continuation과 state를 `__aarch64__` 경계 안에 두고 x86 및 AArch64 PE를 모두 다시 link했다.
- `R12-E0005`: 처음에는 전체 `LUCA.img`를 data disk로 연결해 그 안의 예전 ESP가 먼저 boot했다.
  canonical ext2-only data volume으로 수정해 Ribon이 조립한 ESP를 유일한 boot source로 만들었다.
- `R12-E0006`: 올바른 direct-FDT 실행은 payload를 post-exit에 배치하므로 pre-exit payload marker를
  요구하던 harness가 실제 성공을 timeout으로 오판했다. 이 lane에서만 두 marker를 제외하고
  ExitBootServices, bridge와 LUCA runtime marker를 요구하도록 고쳤으며 25개 unit과 실제 guest를
  다시 통과했다.

## 증거 한계와 다음 owner

Host model은 실제 EL2 register effect, TLB ordering, firmware implementation 차이나 물리 CPU를
입증하지 않는다. AArch64 PE 검사는 실행 증거가 아니다. 실제 QEMU 실행은 EDK2 source EL1 한
조합만 입증한다. Ribon bridge는 영구 higher-half, kernel image W^X, direct map 또는 allocator를
소유하지 않는다. 그 작업은 LUCA parent의 후속 VM 라운드가 소유한다. 이 라운드에서는 R13을
시작하지 않는다.
