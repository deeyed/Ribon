---
doc_type: devlog
status: historical
authority: non-normative
last_verified: 2026-08-02
code_paths:
  - src/common/boot.c
  - src/environments/uefi-app/terminal_image.c
  - src/protocols/os/linux/efi.c
  - products/bootmgr/manifests/x86_64-uefi-linux.json
tests:
  - make check-terminal-image-launch
  - make check-uefi-managed-image
  - make check-linux-x86_64-efi
hardware:
  - none
supersedes:
  - none
---

# R02 generic terminal image launch와 Linux x86_64 EFI

## 구현

- Core ABI에 OS·firmware-neutral terminal image launch authority와 one-shot transaction suffix를
  추가했다.
- UEFI environment provider가 exact source identity, full device path, bounded UTF-16 load option,
  watchdog, returned-child cleanup을 소유한다.
- Linux x86_64 EFI-stub protocol과 독립 product graph를 추가했다.
- OpenWrt 24.10.0 x86/64 kernel을 pinned external build input으로 검증하고 source-owned PID 1
  initramfs와 조합했다.
- Direct-entry Parus UEFI products에는 launcher object가 링크되지 않도록 symbol gate를 추가했다.

## 검증 의미

OVMF QEMU run은 Ribon managed transaction에서 Linux EFI stub으로 넘어가 Linux 6.6.73의 PID 1 marker와
clean poweroff까지 관측한다. 이는 named artifact와 QEMU tuple의 runtime evidence다. FreeBSD, 실기기,
production Secure Boot와 firmware certification evidence가 아니다.
