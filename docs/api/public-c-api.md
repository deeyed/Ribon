---
doc_type: reference
status: accepted
authority: informative
last_verified: 2026-07-26
code_paths:
  - include/Ribon/
tests:
  - ribon-docs
hardware:
  - none
supersedes:
  - none
---

# 공개 C API

이 페이지는 Doxygen XML을 Breathe로 가져온다. Public symbol은 한국어 Doxygen 계약이
있는 경우에만 정본 API 설명으로 노출한다. Legacy OS profile symbol은 API 출력에서
제외한다.

## Core

```{doxygenfile} ribon.h
:project: Ribon
```

## Core service

```{doxygenfile} core.h
:project: Ribon
```

## Platform operation

```{doxygenfile} platform.h
:project: Ribon
```

## Architecture

```{doxygenfile} arch.h
:project: Ribon
```

## Firmware

```{doxygenfile} firmware.h
:project: Ribon
```

## Loader

```{doxygenfile} loader.h
:project: Ribon
```

## Memory

```{doxygenfile} memory.h
:project: Ribon
```

## Profile

```{doxygenfile} profile.h
:project: Ribon
```

## Raspberry Pi

```{doxygenfile} rpi.h
:project: Ribon
```

## UEFI hardening

```{doxygenfile} uefi_hardening.h
:project: Ribon
```
