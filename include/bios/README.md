# Ribon BIOS 인터페이스

이 디렉터리는 legacy BIOS를 향한 C 헤더만 둔다. UEFI 선언이나 Ribon Core 프로파일
API를 여기에 넣지 않는다.

현재 표면은 작게 유지한다.

- E820 memory map 수집 선언
- INT 13h extended disk read packet 선언
- VBE mode information 선언

현재 구현은 `src/firmware/bios` 아래의 unsupported stub으로 연결되어 있다. 실제 x86
BIOS 진입 코드는 BIOS adapter가 구현될 때 이 인터페이스 뒤에 배치한다.
