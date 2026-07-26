# EDK II UEFI 헤더 Import

TianoCore edk2 tag `edk2-stable202605`에서 가져온 헤더이다.

이 디렉터리는 `MdePkg/Include`의 UEFI-facing 헤더로 제한한다.

- architecture binding headers
- `Base.h`, `Uefi.h`, `Uefi/`
- `Guid/`
- `Protocol/`
- `IndustryStandard/`

다음 edk2 헤더 계열은 의도적으로 제외한다.

- `Library/`
- `Ppi/`
- `Pi/`
- `PiDxe.h`, `PiMm.h`, `PiPei.h`, `PiSmm.h`
- `Register/`

edk2 library 구현이나 helper function package는 이 디렉터리에 vendor하지 않는다.
