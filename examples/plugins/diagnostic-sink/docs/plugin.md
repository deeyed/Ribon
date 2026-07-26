# Diagnostic sink external package

이 package는 설치된 Ribon SDK만 소비하는 `SERVICE` plugin 예제다. Caller-owned byte
budget을 넘는 write를 거부하며 network, storage, runtime service를 요구하지 않는다.

`package.json`은 source package를 기술하고 `plugin.qst`는 build-time static composition
surface를 제공한다. `tests/product.json`은 generated immutable registry와 host contract
harness의 library product tuple이다.
