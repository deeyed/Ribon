# Ribos product schema

`schema/`는 compiler, Policy IR validator와 향후 artifact verifier가 공유하는
versioned product identity 계약이다.

Schema는 다음 table을 canonical stable-ID 순서로 인코딩한다.

- product-generated named type
- typed fact/member
- helper path, parameter/result, error type와 capability
- typed handoff field

Canonical encoding은 little-endian length-prefixed byte sequence이고 identity는 그
byte sequence의 SHA-256이다. C pointer, native `size_t`, padding과 link address는
identity에 포함되지 않는다.

현재 `ribon.generic.reference.v1`은 host corpus용 reference schema다. Product/plugin
graph generator가 실제 product schema artifact를 만들고 compiler와 verifier가 같은
artifact를 소비하는 것이 장기 권위다. Policy는 digest가 다른 schema에서 검증된
artifact로 재사용되지 않는다.
