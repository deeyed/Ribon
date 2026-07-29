---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-30
code_paths:
  - language/ribos/grammar/parser.gram
  - language/ribos/grammar/Tokens
  - language/ribos/tools/
  - src/policy/
tests:
  - ribos-grammar-generation
  - ribos-parser-conformance
  - ribos-lexer-conformance
  - ribos-negative-syntax
  - ribos-type-negative
  - ribos-budget-negative
  - ribon-docs
hardware:
  - none
supersedes:
  - informal Python-like policy syntax
  - informal Lua-like policy syntax
---

# Ribos source language 계약

이 계약은 `.ribos` source의 lexical token, grammar, static semantic, evaluation order,
bounded execution과 diagnostic 의무를 정의한다.

## 규범 용어

이 문서에서 다음 용어를 사용한다.

- **허용한다**: conforming compiler가 받아들여야 한다.
- **거부한다**: conforming compiler가 compile failure로 처리해야 한다.
- **동일하다**: 구현별 선택 없이 관찰 가능한 의미가 같아야 한다.
- **implementation limit**: product descriptor나 compiler option에 수치가 기록되고
  artifact에 봉인되는 상한이다.
- **policy fault**: source의 복구 가능한 오류가 아니라 verifier 또는 실행 계약의
  fail-closed 위반이다.

문법은 EBNF와 machine-readable Pegen grammar로 이중 표현한다. 둘이 다르면 이 계약의
EBNF와 static semantic을 먼저 고치고 같은 변경에서 Pegen grammar와 corpus를
동기화해야 한다.

## Source unit

Ribos source file의 확장자는 `.ribos`다. 한 source file은 다음 declaration만 가진다.

- top-level decorator
- top-level function
- top-level struct
- top-level enum

Top-level executable statement, nested function, import와 module initializer는 허용하지
않는다. Product가 여러 source file을 조합하는 방식은 language grammar가 아니라
signed package manifest가 정의한다.

## Character와 encoding

Source는 well-formed UTF-8이다.

- UTF-8 BOM은 거부한다.
- line ending은 LF 또는 CRLF를 허용하고 lexer가 LF로 정규화한다.
- bare CR은 거부한다.
- identifier는 ASCII letter, ASCII digit와 `_`만 사용한다.
- source comment와 string literal에는 UTF-8 scalar value를 사용할 수 있다.
- NUL byte와 invalid UTF-8 sequence는 lexical error다.
- TAB은 string literal 밖에서 lexical error다.
- horizontal whitespace는 U+0020 SPACE다.
- diagnostic column은 정규화된 UTF-8 source의 Unicode scalar index가 아니라 byte
  offset과 1-based source column을 함께 기록한다.

Identifier를 ASCII로 제한하는 것은 firmware symbol, generated C symbol, manifest ID와
diagnostic의 정규화를 단순화하기 위한 규칙이다. String value는 UTF-8을 보존하지만
hardware 또는 protocol ID가 arbitrary string이어야 한다는 뜻은 아니다.

## 줄바꿈

`NEWLINE`은 statement separator다. Indentation은 문법 의미가 없고 `INDENT`와
`DEDENT` token은 존재하지 않는다.

Lexer는 다음 위치의 physical newline을 logical `NEWLINE`으로 만들지 않는다.

- `(`와 대응 `)` 사이
- `[`와 대응 `]` 사이
- `=`, `,`, `.`, `->`, `=>`, `:`, binary operator 뒤
- prefix operator 뒤
- 다음 physical line의 첫 token이 `and` 또는 `or`인 continuation form

`{`와 `}`는 block, struct/enum declaration body와 map literal에 공통으로 사용되므로
brace 안의 newline은 logical `NEWLINE`이 될 수 있다. Map literal grammar는 해당
`NEWLINE`을 `padding`으로 받아들인다. Block을 닫는 `}` 뒤의 newline도 logical
`NEWLINE`이 될 수 있다. Blank line과 comment-only line은 parser에 `NEWLINE` 하나로
전달하거나 제거할 수 있으며 두 방식은 같은 syntax tree를 만들어야 한다.

Backslash line continuation은 허용하지 않는다.

다음 두 source는 같은 syntax tree를 만든다.

```text
let target = slot.selected()
```

```text
let target =
    slot.selected()
```

Boolean expression은 operator를 이전 line 끝이나 다음 line 시작에 둘 수 있다.

```text
let update_allowed =
    ota.available(Channel.STABLE)
    and power.safe()
    and system.on_ground()
```

Delimiter 안에서는 자유롭게 줄을 바꿀 수 있다.

```text
slot.mark_trial(
    target,
    attempts=3,
    commit=False,
)?
```

## Comment

`#`부터 physical line 끝까지 comment다. String literal 안의 `#`는 character다.
Block comment와 nested comment는 제공하지 않는다.

```text
# 이 줄 전체가 comment다.
let target = slot.selected()  # trailing comment다.
let marker = "#not-comment"
```

Comment는 token stream과 artifact에 의미를 주지 않는다. Debug source map은 comment의
source offset을 보존할 수 있다.

## Lexical EBNF

EBNF 표기에서 `"..."`는 terminal, `[...]`는 optional, `{...}`는 zero-or-more,
`(...)`는 grouping, `|`는 alternative다.

```text
letter              = "A" | "B" | "C" | "D" | "E" | "F" | "G"
                    | "H" | "I" | "J" | "K" | "L" | "M" | "N"
                    | "O" | "P" | "Q" | "R" | "S" | "T" | "U"
                    | "V" | "W" | "X" | "Y" | "Z"
                    | "a" | "b" | "c" | "d" | "e" | "f" | "g"
                    | "h" | "i" | "j" | "k" | "l" | "m" | "n"
                    | "o" | "p" | "q" | "r" | "s" | "t" | "u"
                    | "v" | "w" | "x" | "y" | "z" ;

digit               = "0" | "1" | "2" | "3" | "4"
                    | "5" | "6" | "7" | "8" | "9" ;

nonzero_digit       = "1" | "2" | "3" | "4"
                    | "5" | "6" | "7" | "8" | "9" ;

hex_digit           = digit | "a" | "b" | "c" | "d" | "e" | "f"
                            | "A" | "B" | "C" | "D" | "E" | "F" ;

binary_digit        = "0" | "1" ;

identifier          = (letter | "_"), {letter | digit | "_"} ;

decimal_digits      = digit, {["_"], digit} ;
hex_digits          = hex_digit, {["_"], hex_digit} ;
binary_digits       = binary_digit, {["_"], binary_digit} ;

decimal_integer     = "0" | nonzero_digit, {["_"], digit} ;
hex_integer         = "0", ("x" | "X"), hex_digits ;
binary_integer      = "0", ("b" | "B"), binary_digits ;
integer_literal     = decimal_integer | hex_integer | binary_integer ;

escape              = "\\\\" | "\\\"" | "\\n" | "\\r" | "\\t"
                    | "\\0" | "\\x", hex_digit, hex_digit ;

string_character    = any UTF-8 scalar except "\"", "\\", CR, LF and NUL ;
string_literal      = "\"", {string_character | escape}, "\"" ;

comment             = "#", {any scalar except CR and LF} ;
```

숫자 separator `_`는 digit 사이에만 올 수 있다. 다음은 lexical error다.

```text
_10
10_
1__0
0x_FF
```

Ribos는 floating-point literal, character literal, raw string, byte string, string
interpolation과 numeric suffix를 제공하지 않는다.

## Keyword

다음 token은 keyword다.

| Keyword | 의미 |
| --- | --- |
| `and` | short-circuit conjunction |
| `def` | function declaration |
| `else` | alternative branch |
| `enum` | closed enum declaration |
| `False` | boolean false literal |
| `for` | bounded iteration |
| `if` | statement 또는 value conditional |
| `in` | membership comparison과 iteration source |
| `let` | immutable binding declaration |
| `match` | closed enum/result branching |
| `mut` | mutable binding modifier |
| `None` | contextual `Option` empty value |
| `not` | logical negation 또는 `not in` |
| `or` | short-circuit disjunction |
| `return` | function result |
| `struct` | value struct declaration |
| `True` | boolean true literal |

다음 spelling은 reserved keyword이며 program에서 identifier로 사용할 수 없다.

```text
as async await break catch class continue defer except finally from
import lambda raise throw trait try while with yield
```

Reserved keyword에는 실행 의미가 없다. Compiler는 일반 `unexpected token` 대신
`reserved feature is not part of Ribos` diagnostic을 내야 한다.

`Array`, `List`, `FrozenMap`, `Dict`, `Option`, `Result`, scalar type 이름과 `Unit`은
predeclared type name이지 keyword가 아니다. Product-generated type이 같은 이름을
선언할 수는 없다.

## Punctuator와 operator token

Lexer는 가능한 가장 긴 token을 먼저 선택한다.

| Token | Spelling | 용도 |
| --- | --- | --- |
| `AT` | `@` | declaration attribute |
| `LPAR`, `RPAR` | `(`, `)` | parameter, argument, grouping, pattern payload |
| `LSQB`, `RSQB` | `[`, `]` | type argument, array/list literal, index |
| `LBRACE`, `RBRACE` | `{`, `}` | block, declaration body, map literal |
| `COMMA` | `,` | item separator |
| `COLON` | `:` | type annotation, field, map entry |
| `DOT` | `.` | qualified name와 member |
| `RARROW` | `->` | return type |
| `FATARROW` | `=>` | match arm |
| `EQUAL` | `=` | initializer와 assignment |
| `EQEQUAL` | `==` | equality |
| `NOTEQUAL` | `!=` | inequality |
| `LESS`, `LESSEQUAL` | `<`, `<=` | ordered comparison |
| `GREATER`, `GREATEREQUAL` | `>`, `>=` | ordered comparison |
| `PLUS`, `MINUS` | `+`, `-` | integer arithmetic와 unary sign |
| `STAR`, `SLASH`, `PERCENT` | `*`, `/`, `%` | integer arithmetic |
| `AMPER`, `VBAR`, `CIRCUMFLEX` | `&`, `|`, `^` | integer/bitmask operation |
| `TILDE` | `~` | integer/bitmask complement |
| `LEFTSHIFT`, `RIGHTSHIFT` | `<<`, `>>` | checked shift |
| `QUESTION` | `?` | `Option`/`Result` propagation |
| `UNDERSCORE` | `_` | discard pattern when standalone |
| `NEWLINE` | logical line end | statement separator |
| `ENDMARKER` | source end | parser end marker |

Compound assignment, increment/decrement, null-coalescing, optional member access와 range
punctuator는 제공하지 않는다.

## Syntactic EBNF

### Source와 declaration

```text
source_file
    = padding, {decorated_declaration, padding}, ENDMARKER ;

padding
    = {NEWLINE} ;

decorated_declaration
    = {decorator}, declaration ;

declaration
    = function_declaration
    | struct_declaration
    | enum_declaration ;

decorator
    = "@", qualified_name,
      ["(", [attribute_argument_list], ")"],
      NEWLINE ;

attribute_argument_list
    = attribute_argument,
      {",", attribute_argument},
      [","] ;

attribute_argument
    = identifier, "=", constant_expression ;

function_declaration
    = "def", identifier,
      "(", [parameter_list], ")",
      "->", type_expression,
      block ;

parameter_list
    = parameter, {",", parameter}, [","] ;

parameter
    = identifier, ":", type_expression ;

struct_declaration
    = "struct", identifier, "{",
      padding,
      {struct_field, NEWLINE, padding},
      "}" ;

struct_field
    = identifier, ":", type_expression ;

enum_declaration
    = "enum", identifier, "{",
      padding,
      {enum_variant, NEWLINE, padding},
      "}" ;

enum_variant
    = identifier, ["(", [type_list], ")"] ;

type_list
    = type_expression, {",", type_expression}, [","] ;
```

Function parameter와 return type은 생략할 수 없다. `Unit` return도 명시한다.
Struct field와 enum variant 순서는 type identity 및 deterministic encoding의 일부다.

### Block과 statement

```text
block
    = "{", padding, {block_item, padding}, "}" ;

block_item
    = simple_statement, NEWLINE
    | compound_statement, [NEWLINE] ;

simple_statement
    = let_statement
    | assignment_statement
    | return_statement
    | expression_statement ;

compound_statement
    = if_statement
    | for_statement
    | match_statement ;

let_statement
    = "let", ["mut"], identifier,
      [":", type_expression],
      "=", expression ;

assignment_statement
    = assignable, "=", expression ;

assignable
    = identifier
    | postfix_expression ;

return_statement
    = "return", [expression] ;

expression_statement
    = expression ;

if_statement
    = "if", expression, block,
      [padding, "else", (if_statement | block)] ;

for_statement
    = "for", identifier, "in", expression, block ;

match_statement
    = "match", expression, "{",
      padding,
      match_arm, {padding, match_arm},
      padding,
      "}" ;

match_arm
    = pattern, "=>", block ;
```

빈 `match`는 허용하지 않는다. Expression statement의 type은 `Unit`이거나 명시적
discard가 허용된 diagnostic helper result여야 한다.

`return` expression 생략은 return type이 `Unit`일 때만 허용한다.

### Pattern

```text
pattern
    = "_"
    | literal_pattern
    | qualified_name
    | qualified_name, "(", [pattern_argument_list], ")" ;

pattern_argument_list
    = pattern_argument, {",", pattern_argument}, [","] ;

pattern_argument
    = identifier | "_" ;

literal_pattern
    = integer_literal | string_literal | "True" | "False" | "None" ;
```

Pattern은 enum variant, `Option`과 `Result` payload를 한 단계 분해한다. Nested pattern,
range pattern, guard와 user-defined extractor는 제공하지 않는다.

### Type expression

```text
type_expression
    = qualified_name, ["[", type_argument_list, "]"] ;

type_argument_list
    = type_argument, {",", type_argument}, [","] ;

type_argument
    = type_expression | integer_literal ;

qualified_name
    = identifier, {".", identifier} ;
```

`[` 안에서 type과 integer argument를 함께 허용하는 이유는 bounded type 때문이다.

```text
Array[Device, 3]
List[Device, 8]
FrozenMap[BoardRevision, Profile, 2]
Dict[BoardRevision, Profile, 8]
Result[VerifiedImage, VerifyError]
```

User-defined generic type과 generic function은 제공하지 않는다. Type argument syntax는
compiler-provided bounded type과 product-generated type constructor에만 적용된다.

### Expression

```text
expression
    = if_expression | logical_or_expression ;

if_expression
    = "if", logical_or_expression,
      value_block,
      "else",
      value_block ;

value_block
    = "{", padding, expression, padding, "}" ;

logical_or_expression
    = logical_and_expression,
      {"or", logical_and_expression} ;

logical_and_expression
    = logical_not_expression,
      {"and", logical_not_expression} ;

logical_not_expression
    = "not", logical_not_expression
    | comparison_expression ;

comparison_expression
    = bitwise_or_expression,
      [comparison_operator, bitwise_or_expression] ;

comparison_operator
    = "==" | "!=" | "<" | "<=" | ">" | ">="
    | "in" | "not", "in" ;

bitwise_or_expression
    = bitwise_xor_expression,
      {"|", bitwise_xor_expression} ;

bitwise_xor_expression
    = bitwise_and_expression,
      {"^", bitwise_and_expression} ;

bitwise_and_expression
    = shift_expression,
      {"&", shift_expression} ;

shift_expression
    = additive_expression,
      {("<<" | ">>"), additive_expression} ;

additive_expression
    = multiplicative_expression,
      {("+" | "-"), multiplicative_expression} ;

multiplicative_expression
    = unary_expression,
      {("*" | "/" | "%"), unary_expression} ;

unary_expression
    = ("+" | "-" | "~"), unary_expression
    | postfix_expression ;

postfix_expression
    = atom, {postfix_operation} ;

postfix_operation
    = call_suffix
    | member_suffix
    | index_suffix
    | "?" ;

call_suffix
    = "(", [argument_list], ")" ;

member_suffix
    = ".", identifier ;

index_suffix
    = "[", expression, "]" ;

argument_list
    = argument, {",", argument}, [","] ;

argument
    = identifier, "=", expression
    | expression ;

atom
    = integer_literal
    | string_literal
    | "True"
    | "False"
    | "None"
    | list_literal
    | map_literal
    | qualified_name
    | "(", expression, ")" ;

list_literal
    = "[", [expression_list], "]" ;

expression_list
    = expression, {",", expression}, [","] ;

map_literal
    = "{", padding, [map_entry_list], padding, "}" ;

map_entry_list
    = map_entry, {",", map_entry}, [","] ;

map_entry
    = expression, ":", expression ;

constant_expression
    = expression ;
```

`constant_expression`은 grammar상 일반 expression을 사용하지만 attribute context에서
literal, enum value, immutable aggregate와 pure constant constructor만 허용한다.
Helper call, parameter read와 mutable state는 attribute compile-time evaluation에서
거부한다.

Function call에서 positional argument는 named argument보다 앞에 있어야 한다.
Duplicate name, 존재하지 않는 parameter name과 positional/named 중복은 compile error다.
Named argument는 runtime dictionary를 만들지 않고 compiler가 parameter index로
정규화한다.

### Operator precedence

높은 번호가 먼저 평가된다.

| 우선순위 | Operator | 결합 |
| --- | --- | --- |
| 13 | call `()`, member `.`, index `[]`, propagation `?` | left |
| 12 | unary `+`, `-`, `~` | right |
| 11 | `*`, `/`, `%` | left |
| 10 | `+`, `-` | left |
| 9 | `<<`, `>>` | left |
| 8 | `&` | left |
| 7 | `^` | left |
| 6 | `|` | left |
| 5 | `==`, `!=`, `<`, `<=`, `>`, `>=`, `in`, `not in` | non-chainable |
| 4 | `not` | right |
| 3 | `and` | left, short-circuit |
| 2 | `or` | left, short-circuit |
| 1 | expression-if | complete expression |

Python식 chained comparison은 제공하지 않는다.

```text
0 <= value < 8       # 거부
0 <= value and value < 8  # 허용
```

## Static type

### Scalar

Ribos core scalar type은 다음과 같다.

| Type | 의미 |
| --- | --- |
| `bool` | `True` 또는 `False` |
| `u8` | unsigned 8-bit integer |
| `u16` | unsigned 16-bit integer |
| `u32` | unsigned 32-bit integer |
| `u64` | unsigned 64-bit integer |
| `i8` | signed 8-bit two's-complement integer |
| `i16` | signed 16-bit two's-complement integer |
| `i32` | signed 32-bit two's-complement integer |
| `i64` | signed 64-bit two's-complement integer |
| `Unit` | 값이 없는 정상 완료 |

Pointer-sized `usize`, `isize`, native pointer와 floating-point type은 core language에
없다. Physical address가 필요한 helper는 raw integer가 아니라 product-generated
`PhysicalRange`, `MemoryReservation` 같은 opaque type을 사용한다.

Integer literal은 expected type이 있으면 그 type에 정확히 들어가야 한다. Expected
type이 없으면 non-negative literal은 `u32`, `u32`를 넘고 `u64`에 들어가면 `u64`로
추론한다. Unary `-`가 적용된 literal은 `i32`, `i32`를 넘고 `i64`에 들어가면 `i64`로
추론한다. 어느 type에도 들어가지 않으면 compile error다.

Implicit widening, signed/unsigned conversion과 truthiness conversion은 허용하지 않는다.
명시적 checked conversion helper를 사용한다.

```text
let attempts: u32 = 3       # 허용
let percent: u8 = 255       # 허용
let percent: u8 = 256       # 거부
let enabled: bool = 1       # 거부
```

### String와 symbol

String literal은 immutable UTF-8 byte sequence다. Literal 자체는 compile-time
`StringLiteral[N]` type을 가지며 `N`은 decoded UTF-8 byte length다.

Runtime string concatenation, interpolation, substring allocation과 arbitrary string
mutation은 제공하지 않는다. Helper parameter가 다음 중 하나를 명시할 때만 string
literal을 전달할 수 있다.

- bounded `String[N]`
- manifest-generated symbol type
- compile-time-only identifier parameter
- diagnostic sink의 bounded message type

Image, device, channel과 handoff key는 string보다 enum 또는 generated symbol을
사용해야 한다.

```text
image.require(ImageId.DELOS)       # 권장
image.require("delos")             # helper가 literal ID를 선언한 경우에만 허용
```

### Binding과 mutation

모든 local binding은 initializer를 가진다.

```text
let active = slot.active()
let mut target: Slot = active
```

`let` binding은 재대입할 수 없다. `let mut` binding만 `=` assignment의 target이 될 수
있다.

```text
let active = slot.active()
active = slot.inactive()       # 거부

let mut target = slot.active()
target = slot.inactive()       # 허용
```

같은 lexical scope에서 duplicate binding을 허용하지 않는다. 바깥 scope의 이름을
안쪽 scope에서 shadow하지 않는다. `for` binding과 `match` payload binding에도 같은
규칙을 적용한다.

모든 expression은 definite initialization된 값을 읽어야 한다. Ribos에는
uninitialized local과 declaration-only local이 없다.

### Struct

Struct는 field 순서와 type이 고정된 value type이다.

```text
struct UpdateCondition {
    minimum_battery: u8
    require_grounded: bool
}

let condition = UpdateCondition(
    minimum_battery=60,
    require_grounded=True,
)
```

Struct는 다음 의미를 가진다.

- field는 immutable이다.
- Struct construction은 type 이름에 대한 call syntax와 named argument를 사용한다.
- 모든 field argument가 정확히 한 번 나타나야 한다.
- unknown field와 duplicate field는 compile error다.
- field argument의 source 순서는 declaration 순서와 달라도 된다.
- artifact encoder는 declaration 순서로 정규화한다.
- inheritance, method table, hidden header와 object identity가 없다.
- direct 또는 indirect recursive by-value struct를 거부한다.
- field default와 spread initializer를 제공하지 않는다.

Struct field assignment grammar가 형식상 assignable을 만들더라도 immutable field에는
static error를 낸다. Product-generated mutable builder는 opaque helper를 통해서만
변경한다.

### Enum

Enum은 폐쇄된 variant 집합이다.

```text
enum BootMode {
    Normal
    Recovery
    Diagnostic
}

enum ImageCheck {
    Valid(VerifiedImage)
    Invalid(VerifyError)
}
```

Payload가 없는 variant는 `BootMode.Normal`처럼 사용한다. Payload variant는
`ImageCheck.Valid(verified)`처럼 생성하며 `match`에서 분해한다.

Enum은 다음 의미를 가진다.

- variant tag는 declaration 순서와 독립적인 generated stable ID를 artifact에 넣는다.
- source compiler는 duplicate variant를 거부한다.
- payload arity와 type이 정확히 일치해야 한다.
- open enum, unknown variant fallback과 integer에서의 implicit conversion을 제공하지
  않는다.
- Product-generated wire enum은 별도 schema가 지정한 stable value를 사용할 수 있다.

`Option[T]`와 `Result[T, E]`는 compiler-provided closed enum과 같은 의미다.

```text
Option[T]     = None | Some(T)
Result[T, E]  = Ok(T) | Err(E)
```

### Array와 List

Array는 compile-time length가 고정된 immutable aggregate다.

```text
let required_devices = [
    Device.UART0,
    Device.STORAGE0,
    Device.ETH0,
]
```

위 literal은 `Array[Device, 3]`으로 추론한다.

List는 length가 변할 수 있지만 capacity가 type에 고정된 bounded aggregate다.

```text
let mut devices: List[Device, 8] = []
devices.push(Device.UART0)?
```

Array와 List 규칙은 다음과 같다.

- 모든 element type은 동일하다.
- non-empty literal은 element type과 정확한 length를 추론한다.
- empty literal은 expected `Array[T, 0]` 또는 `List[T, N]`이 있어야 한다.
- List length는 `0 <= length <= N`이다.
- List mutation은 mutable binding과 mutating operation이 모두 필요하다.
- capacity를 넘는 operation은 `Result`를 반환한다.
- negative index, slicing, comprehension과 implicit iterator allocation을 제공하지 않는다.
- index가 compile-time 또는 verifier range proof를 통과하지 못하면 checked access가
  `Option[T]`을 반환한다.

Iteration은 element declaration 순서다.

### FrozenMap과 Dict

FrozenMap은 immutable fixed-cardinality map이고 Dict는 mutable fixed-capacity map이다.

```text
let profiles = {
    BoardRevision.V1: Profile.ETH_PHY_A,
    BoardRevision.V2: Profile.ETH_PHY_B,
}
```

위 literal은 `FrozenMap[BoardRevision, Profile, 2]`로 추론한다.

```text
let mut profiles: Dict[BoardRevision, Profile, 8] = {
    BoardRevision.V1: Profile.ETH_PHY_A,
}
```

Map 규칙은 다음과 같다.

- 모든 key type은 동일하다.
- 모든 value type은 동일하다.
- key type은 compiler가 `DeterministicKey`로 승인한 scalar, fieldless enum 또는
  generated symbol이어야 한다.
- duplicate literal key는 compile error다.
- empty map literal은 expected map type이 있어야 한다.
- `FrozenMap[K, V, N]`의 cardinality는 N이다.
- `Dict[K, V, N]`의 capacity는 N이고 runtime cardinality는 `0..N`이다.
- `.get(key)`는 `Option[V]`를 반환한다.
- `.get(key, default=value)`는 `V`를 반환한다.
- insert와 remove는 명시적인 `Result` 또는 `Option`을 반환한다.
- iteration order는 key의 stable total order다.
- randomized hash, process seed와 allocation fallback을 허용하지 않는다.

Implementation은 sorted array, deterministic open addressing 또는 compile-time perfect
map을 사용할 수 있다. 동일 source와 input은 iteration order와 lookup result가
동일해야 한다.

Heterogeneous map과 `Any`는 없다.

```text
let metadata = {
    "board.revision": ctx.board.revision,
    "delos.ready": True,
}
```

위 source는 value type이 서로 달라 compile error다. Handoff metadata에는 typed key를
사용한다.

```text
handoff.set(
    HandoffKey.BOARD_REVISION,
    ctx.board.revision,
)?

handoff.set(
    HandoffKey.DELOS_READY,
    True,
)?
```

### Opaque handle와 typestate

Opaque handle은 user code가 literal, struct 또는 integer cast로 만들 수 없는 type이다.
Helper return과 `match` payload만 handle을 생성한다.

대표 trust transition은 다음과 같다.

```text
Image
    -- image.verify -->
Result[VerifiedImage, VerifyError]

UpdateCandidate
    -- ota.verify_manifest -->
Result[VerifiedUpdate, UpdateError]

InstalledUpdate
    -- update.commit_trial -->
Result[TrialSlot, JournalError]
```

`core.start`, `boot.slot`과 executable transfer helper는 `Image`가 아니라
`VerifiedImage`를 요구해야 한다.

```text
let candidate = image.require(ImageId.DELOS)?
let verified = image.verify(candidate)?
core.start(Core.M7, verified)?
```

다음 flow-sensitive narrowing은 언어 계약으로 제공하지 않는다.

```text
let candidate = image.require(ImageId.DELOS)?

if image.verify(candidate) {
    core.start(Core.M7, candidate)  # 거부
}
```

검증된 handle을 return type으로 만드는 방식이 source control-flow 추론보다 우선한다.

## Function와 call graph

Function은 top-level에만 선언한다.

- parameter와 return type을 모두 명시한다.
- overload를 제공하지 않는다.
- function value, pointer, closure와 indirect call을 제공하지 않는다.
- recursive call과 mutually recursive cycle을 거부한다.
- call graph는 directed acyclic graph다.
- artifact에는 product limit 이하의 maximum call depth를 기록한다.
- 모든 reachable control path는 declared return type의 값을 반환해야 한다.
- Unreachable statement는 compile error다.

Policy entry는 정확히 하나의 `@policy` attribute를 가진 function이다. Helper function은
attribute 없이 선언할 수 있지만 entry에서 reachable하고 effect 검사를 통과해야 한다.

## Attribute

Attribute는 compiler가 인식하는 declaration metadata다. Runtime function이 아니다.

```text
@policy(
    capabilities=[
        Capability.INSPECT,
        Capability.DEVICE,
        Capability.HANDOFF,
        Capability.BOOT,
    ],
    instruction_budget=8192,
    helper_budget=64,
)
def boot(ctx: BootContext) -> Result[BootAction, BootError] {
    ...
}
```

Core attribute set은 다음과 같다.

| Attribute | 적용 대상 | 의미 |
| --- | --- | --- |
| `@policy` | function | policy entry와 capability/budget |
| `@pure` | function | helper effect가 없는 pure function |
| `@test` | function | host conformance entry |

Product extension attribute는 signed schema가 stable qualified name, argument type와
적용 declaration kind를 정의해야 한다.

Attribute는 다음을 할 수 없다.

- declaration을 다른 declaration으로 변환
- arbitrary source expression 실행
- function wrapping
- runtime call 삽입
- 새로운 identifier와 type 생성
- capability manifest보다 넓은 effect 부여

Unknown attribute와 unknown argument는 compile error다.

## Expression semantic

### Evaluation order

다음 순서를 고정한다.

- function receiver를 먼저 평가한다.
- positional argument를 source 순서대로 평가한다.
- named argument를 source 순서대로 평가한 뒤 parameter index에 정규화한다.
- binary operator의 left operand를 right operand보다 먼저 평가한다.
- collection literal item과 map entry를 source 순서대로 평가한다.
- struct constructor argument를 source 순서대로 평가한 뒤 declaration field 순서로
  저장한다.

Pure expression이라도 compiler가 observable helper call, fault point와 diagnostic source
order를 바꾸어서는 안 된다.

### Short circuit

`and`는 left가 `False`이면 right를 평가하지 않는다. `or`는 left가 `True`이면 right를
평가하지 않는다. 두 operand는 `bool`이어야 한다.

```text
let update_allowed =
    ota.available(Channel.STABLE)
    and ctx.power.battery_percent >= 60
    and ctx.system.on_ground
```

`ctx.system.on_ground`는 앞의 조건이 모두 `True`일 때만 읽힌다.

### Expression-if

Expression-if는 두 branch가 같은 type이어야 하며 반드시 `else`를 가진다.

```text
let profile = if ctx.board.revision >= 3 {
    Profile.ETH_PHY_B
} else {
    Profile.ETH_PHY_A
}
```

Value block은 expression 하나만 가진다. Side-effect statement, local binding과 return을
넣을 수 없다.

Python식 conditional expression은 거부한다.

```text
let profile =
    Profile.ETH_PHY_B if revision >= 3 else Profile.ETH_PHY_A
```

### Comparison와 membership

Comparison operand는 같은 type이거나 명시적으로 승인된 pair여야 한다.

`in`과 `not in`은 다음 bounded type에만 적용한다.

- Array와 List
- FrozenMap과 Dict의 key set
- product-generated bounded set

User-defined membership operator와 iterator protocol은 없다. Membership의 최대 비교
횟수는 collection capacity로 제한된다.

### Arithmetic

Integer operation은 declared width에서 수행한다.

- overflow와 underflow는 wrap하지 않는다.
- division by zero를 허용하지 않는다.
- shift count는 `0 <= count < bit_width`여야 한다.
- right shift of signed negative value는 arithmetic shift로 정의한다.
- recoverable arithmetic이 필요하면 `checked_add`, `checked_div` 같은 typed helper가
  `Result`를 반환한다.

Unchecked expression의 invalid arithmetic은 catch할 수 없는 policy fault다. Compiler가
constant expression에서 invalid arithmetic을 발견하면 compile error를 낸다.

## Control flow와 bound

### If

Statement `if` condition은 `bool`이어야 한다.

```text
if network.ready() and power.safe() {
    update.poll()?
} else {
    diagnostic.note(Diagnostic.UPDATE_SKIPPED)?
}
```

### For

`for`는 bounded iterable만 허용한다.

```text
let required_devices = [
    Device.UART0,
    Device.STORAGE0,
    Device.ETH0,
]

for dev in required_devices {
    device.init(dev, Profile.DEFAULT)?
}
```

Maximum iteration은 source type의 capacity다. List가 두 element만 가지더라도
`List[T, 8]`을 순회하는 verifier bound는 8이다.

`range(start, end)`는 compiler intrinsic이며 두 bound가 compile-time integer일 때만
허용한다.

```text
for attempt in range(0, 3) {
    recovery.probe(attempt)?
}
```

`while`, `break`, `continue`, user-defined iterator와 generator는 제공하지 않는다.

### Match

`match`는 closed enum의 모든 reachable variant를 처리해야 한다. `_` arm은 마지막에만
둘 수 있고 그 앞의 arm과 중복되면 compile error다.

```text
match image.verify(candidate) {
    Ok(verified) => {
        core.start(Core.M7, verified)?
        handoff.set(HandoffKey.DELOS_READY, True)?
    }

    Err(reason) => {
        diagnostic.record_verify_error(reason)?
        handoff.set(HandoffKey.DELOS_READY, False)?
    }
}
```

Pattern binding은 arm block 안에서 immutable local이다.

### Propagation operator

Postfix `?`는 `Result`와 `Option`에만 적용한다.

`Result[T, E]`에 대한

```text
let value = operation()?
```

은 다음 의미다.

```text
match operation() {
    Ok(value) => {
        # value를 surrounding expression에 제공한다.
    }

    Err(error) => {
        return Err(error)
    }
}
```

호출 function의 return type이 같은 error를 수용하지 못하면 compile error다.
Product-generated explicit error conversion function을 호출한 뒤 전파할 수 있다.

`Option[T]`의 `?`는 enclosing function도 `Option[U]`를 반환할 때만 `None`을
전파한다. `Option`을 `Result`로 암묵 변환하지 않는다.

## Effect와 capability 검사

각 helper signature는 다음 metadata를 가진다.

```text
helper ID
parameter와 return type
required capability
effect class
maximum input/output byte
maximum operation count
deadline class
allowed lifecycle phase
```

Compiler는 entry에서 reachable한 helper의 capability 합집합이 `@policy` declaration을
넘지 않는지 검사한다. Verifier는 artifact의 call table과 product descriptor를 다시
검사한다.

다음은 compile 또는 verification failure다.

- `NETWORK` capability 없이 network helper 호출
- `FLASH` capability 없이 inactive-slot write 호출
- Normal product policy에서 recovery-only helper 호출
- quiesce 뒤 Environment service 호출
- helper input bound를 넘는 container 또는 string 전달
- helper call budget을 넘는 static bound

Raw MMIO, raw flash offset, raw pointer와 arbitrary jump helper는 capability를 선언해도
public Ribos helper set에 들어올 수 없다.

## Handoff rule

Handoff는 heterogeneous map이 아니라 protocol schema다.

```text
handoff.set(
    HandoffKey.BOARD_REVISION,
    ctx.board.revision,
)?
```

Compiler는 key별 value type, multiplicity, lifecycle과 owner를 schema에서 읽는다.

```text
HandoffKey.BOARD_REVISION -> BoardRevision, singleton
HandoffKey.DELOS_READY    -> bool, singleton
HandoffKey.RESERVED_RANGE -> MemoryReservation, repeated bounded
```

String index assignment은 Dict에만 적용하며 HandoffBuilder에 적용할 수 없다.

```text
handoff["delos.ready"] = True  # type error
```

## Exception와 fault boundary

다음 syntax는 lexical 또는 parse error다.

```text
try
except
finally
raise
throw
catch
```

Ribos runtime에는 stack unwinding, exception object, handler table과 catchable trap이
없다.

복구 가능한 I/O, device, verification, update와 journal failure는 `Result`로
표현한다. Program이 `Result`를 무시하면 compile error다.

Policy fault는 다음 순서를 따른다.

```text
fault detect
  -> mutable policy execution state 폐기
  -> bounded fault receipt 봉인
  -> external update writer와 normal network authority 철회
  -> product-defined factory recovery
```

Policy source는 fault를 catch하거나 recovery target을 바꿀 수 없다.

## Full execution example

다음 예시는 device initialization, auxiliary firmware, update, slot fallback와 typed
handoff를 함께 사용한다.

```text
struct UpdateCondition {
    minimum_battery: u8
    require_grounded: bool
}

enum BootMode {
    Normal
    Recovery
    Diagnostic
}

@pure
def select_eth_profile(
    revision: BoardRevision,
) -> Profile {
    return if revision >= BoardRevision.V3 {
        Profile.ETH_PHY_B
    } else {
        Profile.ETH_PHY_A
    }
}

@policy(
    capabilities=[
        Capability.INSPECT,
        Capability.DEVICE,
        Capability.NETWORK,
        Capability.FLASH,
        Capability.STATE,
        Capability.HANDOFF,
        Capability.BOOT,
    ],
    instruction_budget=8192,
    helper_budget=64,
)
def boot(
    ctx: BootContext,
) -> Result[BootAction, BootError] {
    device.init(Device.UART0, Profile.DEBUG)?
    device.init(Device.STORAGE0, Profile.DEFAULT)?

    let eth_profile = select_eth_profile(
        ctx.board.revision,
    )

    device.init(Device.ETH0, eth_profile)?

    let delos_candidate = image.require(ImageId.DELOS)?

    match image.verify(delos_candidate) {
        Ok(delos) => {
            core.start(Core.M7, delos)?
            handoff.set(HandoffKey.DELOS_READY, True)?
        }

        Err(reason) => {
            diagnostic.record_verify_error(reason)?
            handoff.set(HandoffKey.DELOS_READY, False)?
        }
    }

    let update_condition = UpdateCondition(
        minimum_battery=60,
        require_grounded=True,
    )

    let update_allowed =
        ota.available(Channel.STABLE)
        and ctx.power.battery_percent >= update_condition.minimum_battery
        and ctx.system.on_ground == update_condition.require_grounded

    if update_allowed {
        let inactive = slot.inactive()
        let receipt = ota.install(
            Channel.STABLE,
            inactive,
        )?

        slot.mark_trial(
            receipt,
            attempts=3,
            commit=False,
        )?
    }

    let mut target = slot.selected()

    if slot.failures(target) >= 3 {
        slot.mark_bad(target)?
        target = slot.previous_good()?
    }

    let candidate = slot.image(target)?
    let verified = image.verify(candidate)?

    handoff.set(
        HandoffKey.BOARD_REVISION,
        ctx.board.revision,
    )?

    handoff.set(
        HandoffKey.BOOT_SLOT,
        target,
    )?

    return Ok(boot.slot(target, verified))
}
```

이 program의 최대 loop count는 0이고 maximum function call depth는 2다. Helper budget은
short-circuit와 branch를 고려한 최악 경로로 계산한다. `ota.install`이 호출되지 않는
경로의 실제 helper count가 작더라도 artifact에는 최악 경로 상한을 기록한다.

## Collection execution example

```text
@policy(
    capabilities=[
        Capability.DEVICE,
        Capability.BOOT,
    ],
    instruction_budget=2048,
    helper_budget=16,
)
def initialize_required_devices(
    ctx: BootContext,
) -> Result[BootAction, BootError] {
    let required_devices: Array[Device, 3] = [
        Device.UART0,
        Device.STORAGE0,
        Device.ETH0,
    ]

    for dev in required_devices {
        device.init(dev, Profile.DEFAULT)?
    }

    let target = slot.selected()
    let candidate = slot.image(target)?
    let verified = image.verify(candidate)?

    return Ok(boot.slot(target, verified))
}
```

Loop body의 helper count는 Array length 3을 곱해 계산한다. Runtime input이 iteration
count를 3보다 크게 만들 수 없다.

## Map execution example

```text
@pure
def profile_for_revision(
    revision: BoardRevision,
) -> Profile {
    let profiles: FrozenMap[BoardRevision, Profile, 2] = {
        BoardRevision.V1: Profile.ETH_PHY_A,
        BoardRevision.V2: Profile.ETH_PHY_B,
    }

    return profiles.get(
        revision,
        default=Profile.ETH_PHY_A,
    )
}
```

Lookup은 maximum cardinality 2로 bounded된다. 동일 key와 map은 product, architecture와
compiler implementation에 관계없이 같은 result를 만든다.

## Rejected source example

### Immutable binding reassignment

```text
let target = slot.selected()
target = slot.previous_good()?
```

Diagnostic category는 `E_MUTATE_IMMUTABLE_BINDING`이다.

### Heterogeneous map

```text
let metadata = {
    "delos.ready": True,
    "boot.slot": slot.selected(),
}
```

Diagnostic category는 `E_MAP_VALUE_TYPE_MISMATCH`다.

### Unbounded loop

```text
while network.ready() {
    update.poll()
}
```

`while`은 reserved feature이므로 `E_RESERVED_FEATURE`다.

### Exception

```text
try {
    ota.install(Channel.STABLE, slot.inactive())
} catch error {
    boot.recovery(RecoveryReason.UPDATE_FAILED)
}
```

`try`와 `catch`는 reserved feature이므로 `E_RESERVED_FEATURE`다.

### Unverified transfer

```text
let candidate = slot.image(slot.selected())?
return Ok(boot.slot(slot.selected(), candidate))
```

`boot.slot`이 `VerifiedImage`를 요구하므로 `E_ARGUMENT_TYPE_MISMATCH`다.

### Unknown capability

```text
@policy(
    capabilities=[Capability.INSPECT],
    instruction_budget=1024,
    helper_budget=4,
)
def fetch(ctx: BootContext) -> Result[Unit, NetworkError] {
    network.fetch_signed_manifest(Channel.STABLE)?
    return Ok(Unit)
}
```

`NETWORK` capability가 없으므로 `E_CAPABILITY_NOT_DECLARED`다.

### Unsupported Python conditional

```text
let profile = Profile.B if revision >= 3 else Profile.A
```

Diagnostic category는 `E_UNSUPPORTED_CONDITIONAL_FORM`이다.

### Empty inferred collection

```text
let devices = []
let profiles = {}
```

두 declaration 모두 expected type이 없어 `E_CANNOT_INFER_EMPTY_COLLECTION`이다.

## Diagnostic contract

Compiler diagnostic은 최소한 다음 정보를 가진다.

```text
stable diagnostic category
source file ID
start byte offset와 end byte offset
1-based line와 column
primary message
expected token 또는 expected type
bounded related span 목록
```

Parser는 farthest-progress token에서 syntax error를 보고한다. Invalid-rule second pass를
사용하더라도 diagnostic 결과는 product limit 안에서 bounded되어야 한다.

Compiler가 source snippet을 출력할 때 secret manifest value와 runtime credential을
포함해서는 안 된다.

## Formatter contract

Canonical formatter는 다음 style을 사용한다.

- indentation은 space 4개다.
- TAB을 출력하지 않는다.
- statement 끝에 semicolon을 출력하지 않는다.
- trailing comma는 multiline parameter, argument와 literal에 사용한다.
- `{`는 declaration 또는 condition과 같은 logical line에 둔다.
- `else`는 closing brace와 같은 logical line에 둔다.
- line width 기본값은 88 byte이며 string literal을 자동 분할하지 않는다.
- `let mut` 사이에는 다른 token을 넣지 않는다.
- Boolean과 None은 `True`, `False`, `None`으로 표기한다.

Formatter는 comment를 보존하지만 source 의미를 바꾸지 않는다.

## Conformance boundary

Ribos source conformance는 다음 evidence를 각각 요구한다.

1. EBNF와 Pegen grammar의 construct mapping
2. Positive syntax corpus parse
3. Negative syntax corpus의 stable diagnostic category
4. Formatter parse-format-parse AST equality
5. Type와 mutation negative corpus
6. Bounded loop와 call graph negative corpus
7. Capability와 typestate negative corpus
8. Generated C parser의 reproducible source digest

Parser generation 성공은 type checker, policy IR, bytecode verifier, VM execution 또는
firmware product integration 성공을 뜻하지 않는다.
