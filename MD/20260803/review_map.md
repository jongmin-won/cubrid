# 코드 리뷰 가이드 — NUMERIC 집계 POC

브랜치 `poc-numeric-sum-acc` · 2026-08-07 기준 HEAD `3c9550d29`
설계·측정·기각 사유는 `poc_numeric_sum_acc_results.md`, 여기는 **어디를 어떤 눈으로 볼 것인가**만 담는다.

## 0. 리뷰 범위

| | |
|---|---|
| 기준 커밋 | `c27d4754c` — CBRD-27150의 마지막 커밋. **그 뒤가 전부 POC** |
| 커밋 수 | 41 |
| 변경 | `src/` **17파일 / +2018 −95** |
| GitHub | `https://github.com/jongmin-won/cubrid/compare/c27d4754c...poc-numeric-sum-acc` |
| 로컬 | `git diff c27d4754c..HEAD -- src/` |

`develop`을 기준으로 잡으면 1144 커밋이 섞여 보인다 — `develop` 라벨이 낡았기 때문이다. 반드시
위 기준 커밋을 쓸 것.

**게이트**: 전 구간이 `CUBRID_NUMSUM_ACC=1` 환경변수 뒤에 있다. off면 as-is와 동일해야 한다 —
"게이트 밖으로 새는 코드가 없는가"가 리뷰의 첫 질문이다.

## 1. 읽는 순서

새 개념이 세 겹으로 쌓여 있어서, 아래 순서로 읽으면 각 층이 앞 층에만 의존한다.

```
① 자료구조      xasl_aggregate.hpp, xasl_analytic.hpp, numeric_opfunc.h
                  → 누산기·체인 값이 어떻게 생겼는지
② 산술 코어      numeric_opfunc.c
                  → 누산기에 더하고, 체인으로 계산하고, 마지막에 pack
③ 집계 실행      query_aggregate.cpp
                  → 행마다 누적, 공유 링크, 그룹 끝에서 finalize
④ 수식 평가      fetch.c + regu_var.hpp
                  → 집계 인자 수식을 체인으로 (플래그로 범위 한정)
⑤ 경로 연결      query_executor.c, px_scan_result_handler.cpp, px_scan_task.cpp,
                 stream_to_xasl.c
                  → 마킹을 직렬·병렬 양쪽에서, 워커 사본은 재마킹, 스트림 복원 시 초기화
⑥ 분석 함수      query_analytic.cpp
                  → 같은 누산기를 비파괴 스냅샷으로
```

## 2. 파일별 진입점

줄 번호는 2026-08-07 HEAD(`3c9550d29`) 기준이며 변할 수 있다 — 함수명으로 찾는 것이 안전하다.

### ① 자료구조

| 파일 | 줄 | 무엇 |
|---|---|---|
| `xasl_aggregate.hpp` | `aggregate_accumulator` | `num_sum_acc`(워드 누산기) + `shared_from`(공유 인덱스, 1-based). **구조체가 32 → 168바이트가 된다** — 해시 집계 엔트리에 그대로 얹히므로 게이트를 꺼도 남는 비용이다(결과 문서 §5) |
| `xasl_analytic.hpp` | `analytic_list_node` | `num_sum_acc` — 분석 노드에도 같은 누산기 |
| `numeric_opfunc.h` | — | `NUMERIC_SUM_ACC`(14워드=254자리), `NUMERIC_POC_CHAIN_VAL`(u128 계수+scale+부호) |

### ② 산술 코어 — `numeric_opfunc.c` (+877 −9, 가장 큰 변경)

| 줄 | 함수 | 역할 |
|---|---|---|
| — | `numeric_init_poc_gate` | `__attribute__((constructor))` — 게이트·pow10 표를 **로드 시점 1회** |
| — | `numeric_poc_gate_enabled` | 게이트 조회 (분기 하나) |
| 4131 | `numeric_sum_acc_add_value` | **누적 진입** — 첫 값이면 누산기 초기화(memset+scale), 이후엔 더하기 |
| 4194 | `numeric_sum_acc_add_acc` | 누산기끼리 합침 — **워커 부분합 병합**이 여기로 온다 (§3-1 #2) |
| 4232 | `numeric_sum_acc_accumulate` | 시드 로직 1벌 — 첫 값·스필 복귀 판정을 여기로 모았다 |
| 4383 | `numeric_sum_acc_add_core` | 워드 덧셈·뺄셈 (인트린식 캐리). `val_used` 기반, 워드 수 가변 |
| 4541 | `numeric_poc_chain_from_dbv` | NUMERIC DB_VALUE → 체인 값 (w[0]≠0이면 128비트 초과 → false) |
| 4585 | `numeric_poc_chain_from_int_dbv` | **정수 → 체인 값** (scale 0). `T_CAST_WRAP`을 건너뛰는 근거가 주석에 |
| 4627 | `numeric_poc_chain_mul` | 곱셈 — scale 상·하한 + `__builtin_mul_overflow` |
| 4668 | `numeric_poc_chain_add` | 덧·뺄셈 — scale 차 38 초과 거부, `__builtin_add_overflow` |
| 4731 | `numeric_poc_chain_to_dbv` | 체인 값 → DB_VALUE (**여기서만** 자릿수 계산) |
| 4759 | `numeric_sum_acc_snapshot` | 누산기 → 값, **누산기 보존** (분석 전용) |
| 4824 | `numeric_sum_acc_finalize` | 누산기 → 값, **누산기 종료** (집계 전용) |

### ③ 집계 실행 — `query_aggregate.cpp` (+505)

| 줄 | 지점 | 역할 |
|---|---|---|
| 129 | `qdata_numeric_sum_acc_enabled` | 게이트 |
| 146 | `qdata_poc_eval_word_chain` | 체인 평가기 **둘 중 하나** (§3-2). `T_CAST_WRAP`도 흡수한다 |
| 289 | `qdata_aggregate_accumulator_to_accumulator` | **워커 부분합 병합.** `case PT_SUM`(310~)이 3갈래 — 양쪽 살아있으면 워드로 직결, 받는 쪽이 비었으면 통째 인수, 아니면 finalize. 여기서 값으로 병합하면 워커 수마다 답이 달라진다 |
| 795 | `qdata_agg_may_share_accumulator` | 공유 가능 판정 (SUM/AVG ∧ 비DISTINCT ∧ sort_list 없음 ∧ …) |
| 828 | `qdata_link_shared_accumulators` | **쿼리당 1회** 공유 링크 (`shared_from = owner_index + 1`) |
| 1662 | `qdata_propagate_shared_accumulators` | **값 확정(finalize) 전** 공유 상태 복사 |
| 1715 | `qdata_finalize_aggregate_list` | 그룹 마무리 — 첫 줄이 위 전파, 그 뒤 누산기 → 값 |

### ④ 수식 평가 — `fetch.c` (+245), `regu_var.hpp` (+1)

| 줄 | 지점 | 역할 |
|---|---|---|
| 99 | `fetch_poc_chain_shape_ok` | 최상위가 `{+,−,×}`인지 — **쿼리당 1회**(마킹 시점) 호출 |
| 119 | `fetch_poc_chain_int_source` | 감싸인 쪽이 SHORT/INTEGER/BIGINT인지 |
| 145 | `fetch_poc_chain_node_ok` | 재귀 본체. `T_CAST_WRAP` + 정수 + 대상 도메인 (40,0)이면 **리프로 인정** |
| 227 | `fetch_poc_eval_chain` | 트리를 재귀로 걸으며 체인 계산 (행마다). 캐스트는 실행하지 않고 자식 정수를 읽는다 |
| 328 | `fetch_peek_arith` 안 훅 | 플래그 + 결과 도메인만 보고 진입 |
| `regu_var.hpp` | `REGU_VARIABLE_AGG_OPERAND = 0x2000` | 집계 인자로만 소비되는 수식 표시 |

### ⑤ 경로 연결

| 파일 | 줄 | 역할 |
|---|---|---|
| `query_executor.c` | 21223 `qexec_mark_aggregate_operand_expressions` | 게이트·shape 판정 후 플래그. GROUP BY 수식과 BUILDVALUE 수식 양쪽을 훑는다 |
| 〃 | 15950–15951 | **직렬 GROUP BY** 호출 (마킹 + 공유 링크) |
| 〃 | 15979 | **직렬 BUILDVALUE** 호출 |
| 〃 | 1215 | 해시 GROUP BY 진입 시 공유 링크 |
| `px_scan_result_handler.cpp` | 843–844 | **병렬 워커** 호출 — 이 2줄이 없으면 병렬에서 전부 무효 |
| `px_scan_task.cpp` | 622 | **워커 사본 재마킹** — `clone_xasl`이 런타임 regu 플래그를 옮기지 않는다(§3-1 #8) |
| `stream_to_xasl.c` | 5970–5971 | **CS 모드 결함 수정** — 스트림 복원 시 `shared_from = 0`, `is_active = false` |

### ⑥ 분석 함수 — `query_analytic.cpp` (+179)

| 줄 | 지점 | 역할 |
|---|---|---|
| 84–87 | `qdata_initialize_analytic_func` | `is_active = false` 초기화 |
| 175–186 | 인자 peek 조기 경로 | 누산기가 이미 돌면 peek해서 바로 누적 |
| 468 | 누적 본체 | `numeric_sum_acc_accumulate ()` — 집계와 **같은 시드 로직**. 첫 값·NULL·비NUMERIC은 기존 경로 |
| 1065–1067 | `snapshot` 호출 | 행마다 중간값을 내면서 누산기는 살려둔다 |

**분석에는 워드 체인이 걸려 있지 않다.** 인자 수식은 정렬 경계 때문에 이미 물질화되어
`a_val_list` 슬롯으로 오고, 다섯 경로를 시도했지만 전부 발동 0회였다. 사유는
`query_executor.c`의 주석과 결과 문서에 있다 — 리뷰에서 "집계에만 있고 분석에 없다"를
누락으로 읽지 않도록.

## 3. 리뷰 관점 — 무엇을 의심할 것인가

### 3-1. 이 POC에서 실제로 터진 결함 유형 (같은 눈으로 볼 것)

| # | 질문 | 근거 |
|---|---|---|
| 1 | **병렬 경로에도 넣었는가** | 워커는 자기 XASL·자기 해시 컨텍스트를 쓴다. 직렬만 고치면 조용히 무효 → 실제로 3번 발생 |
| 2 | **값 꺼내는 지점을 다 찾았는가** | 최종 출력 / 해시 스필 / 부분합 병합 — 3곳 |
| 3 | **새 필드가 0으로 시작해도 안전한가** | 실행 전용 필드는 초기화가 모든 경로에 닿지 않는다 → 인덱스는 1-based |
| 4 | **배열 인덱스의 하한도 검사하는가** | `<= 0` (음수 방어) |
| 5 | **게이트 안에 있는가** | off에서 as-is와 100% 동일해야 한다 → 2번 위반한 적 있음 |
| 6 | **`er_set` 없이 실패하지 않는가** | 없으면 `Query execution failure #NNNNN`만 남아 진단 불가 |
| 7 | **스트림 XASL 경로를 고려했는가** | `stx_alloc_struct()`는 memset하지 않는다 → CS 모드에서만 터짐 |
| 8 | **워커 사본에 런타임 플래그가 따라갔는가** | `clone_xasl`(xcache 복제·`stx_map_stream_to_xasl`)은 **새 트리**를 만들고 실행 중에 붙인 regu 플래그를 옮기지 않는다. 사본에서 다시 판정해야 한다. 플래그를 복사하는 방식은 금지 — `FETCH_ALL_CONST`까지 따라와 워커가 미기입 `arithptr->value`를 반환한다 |
| 9 | **기존 함수 한가운데서 조기 반환하는가** | `fetch_peek_arith ()`는 **끝에서 반드시** 상수성 플래그를 설정하고 호출자가 그걸 assert한다. 건너뛰면 **debug 빌드에서 서버 즉사**(assert는 stderr로 나가고 서버 stderr는 버려져 로그 흔적이 없다). release는 정확한 결과를 내므로 TC 전부 통과하고도 숨는다 |
| 10 | **결과 도메인으로 입력 타입을 거르지 않았는가** | `aggregate_list_node::domain`은 *결과* 도메인이고 **`AVG`는 입력과 무관하게 항상 DOUBLE**. 여기에 NUMERIC 조건을 걸었다가 `AVG(numeric)`이 전부 배제돼 Q1 −18% 회귀 |

### 3-2. 헷갈리기 쉬운 지점

**체인 평가기가 두 개 있다.** 같은 일을 하는 중복이 아니라 진입 경로가 다르다:

| | `fetch.c: fetch_poc_eval_chain` | `query_aggregate.cpp: qdata_poc_eval_word_chain` |
|---|---|---|
| 언제 | 출력 수식(outptr)을 평가할 때 | 집계 노드 operands가 **직접** `TYPE_INARITH`일 때 |
| 리프 값 | `fetch_peek_dbval`로 그 자리에서 얻음 (`thread_p`·`vd`·`tpl` 필요) | 이미 평가된 `dbvalptr`/`dbval`만 읽음 |
| `T_CAST_WRAP` | 자식 정수를 그 자리에서 peek | 자식이 `TYPE_CONSTANT`/`TYPE_DBVAL`일 때만 (스레드 문맥이 없다) |
| Q1에서 | **주 경로** (프로파일 5.1%) | 프로파일에 안 나타남 |

리뷰 시 확인할 것: 두 경로가 **같은 폴백 규칙**을 쓰는지, 한쪽만 고치고 다른 쪽을 놓치지 않았는지.
모양 검사(`fetch_poc_chain_shape_ok`)는 **둘이 공유**하므로, 한쪽이 못 다루는 모양을 통과시키면
그쪽은 false를 돌려 폴백한다 — 오답은 아니지만 헛걸음이다.

**캐스트는 실행하지 않는다.** `T_CAST_WRAP` 노드를 만나면 그 노드를 평가하는 대신 **자식 정수를
읽어 scale 0으로 싣는다.** 근거는 대상 도메인이 아니라 소스가 정수라는 사실이다 — 도메인 (40,0)은
DB_VALUE 세계에서 *float numeric 표식*이라 실제 scale이 헤더에 있고 아무것도 보장하지 않는다
(`db_get_numeric_scale ()`이 그렇게 읽는다). 리뷰 포인트는 세 가지: ①`T_CAST_WRAP`만인가(명시적
`T_CAST`를 건너뛰면 좁히기 오버플로 에러를 삼킨다) ②대상 도메인이 (40,0)인가
(`pt_eval_expr_type ()`은 `PT_TYPE_MAYBE`를 상대 피연산자 타입으로 감싸므로 스케일이 붙을 수 있다)
③런타임에 값의 실제 타입을 다시 보는가.

**`snapshot`과 `finalize`의 차이**: 집계는 그룹당 1회이므로 누산기를 끝내지만(`finalize`), 누적
분석은 행마다 중간값이 필요하므로 누산기를 보존해야 한다(`snapshot`). 둘을 뒤바꾸면 분석 결과가
두 번째 행부터 깨진다.

**폴백은 행 단위다.** 1행은 체인, 2행은 폴백, 3행은 다시 체인이 될 수 있다. "한 번 폴백하면
끝까지 폴백"이라고 읽으면 안 된다.

### 3-3. 정확성에서 합의된 예외

**합계가 40자리를 넘는 SUM은 결과가 바뀐다** — to-be가 수학적으로 옳다(as-is는 행마다 반올림해
드리프트). 이건 버그가 아니라 스펙 변경이며, QA answer 갱신이 필요한 항목이다. 그 외에는
비트 단위로 같아야 한다.

## 4. 리뷰 중 직접 확인하는 방법

```bash
# 게이트 A/B — 서버 프로세스가 환경변수를 가져야 한다
cubrid server stop <db>
export CUBRID_NUMSUM_ACC=1          # 또는 unset
cubrid server start <db>
tr '\0' '\n' < /proc/$(pgrep -x cub_server)/environ | grep NUMSUM   # 실제 적용 확인
```

| 확인 | 방법 |
|---|---|
| **CS 모드로 볼 것** | `csql -u dba <db>`. `csql -S`(SA)는 XASL 스트림 경로를 타지 않아 결함을 숨긴다 |
| 바이너리 반영 | `ls -l $CUBRID/bin/cub_server` — `build.sh install`은 실행 트리를 갱신하지 않는다 |
| 정확성 | 검증 세트 101개를 게이트 on/off로 돌려 diff. 차이는 40자리 결과만 나와야 한다 |
| 스필 경로 | `cubrid.conf`에 `max_agg_hash_size=32768`(하한값) 후 재시작 — 세션 변경은 거부된다 |
| 성능 | 병렬 Q1 노이즈 ±1.5s. **CPU 4% 미만 개선은 병렬 wall로 검증 불가** → 직렬로 재야 1:1로 보인다 |
| 성능 A/B | **양쪽이 같은 코드를 도는 질의를 대조군으로 끼울 것.** 서로 다른 빌드는 동일 동작에도 6% 차이가 났고, 그 눈금자가 없었으면 헛된 결론을 낼 뻔했다 |
| 해시 집계 | 트레이스의 `hash`·`page`를 스필 지표로 읽지 말 것 — 전자는 선택도 포기, 후자는 정렬 페이지다. 자동 병렬도 `hash`를 덮어쓴다(`max_parallel_workers=0` 필요, `PARALLEL(1)` 힌트로는 안 꺼진다). 결과 문서 §5 참조 |

## 5. 아직 열려 있는 항목

| 항목 | 상태 |
|---|---|
| 코드 다듬기 | pow10 표 네이밍(`_gv_` 통일), `numeric_poc_digits_u128`과 기존 `float_numeric_get_decimal_digit` 중복 검토 |
| 해시 엔트리 크기 | 누산기를 박아 엔트리 140 → 276바이트, 축출 시작 15,000 → 7,600 그룹. **게이트와 무관**하나 wall로는 안 나타남. 지연 할당 여부는 본구현 판단 — 결과 문서 §8 2-1 |
| 분석 함수 체인 | 미적용 확정. 5회 시도 전부 실패했고 사유는 `query_executor.c`의 주석과 결과 문서에 |
| Jira 이슈화 | 40자리 초과 SUM 결과 변경(QA answer 갱신), 게이트 제거 |
| 기각·되돌림 완료 | 해시 델타 스킵(`5bec86046` → `623ebfe6e`), 캐스트를 DB_VALUE 리프로 흡수(미커밋 제거) — 사유는 결과 문서 §4 |
