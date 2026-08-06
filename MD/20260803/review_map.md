# 코드 리뷰 가이드 — NUMERIC 집계 POC

브랜치 `poc-numeric-sum-acc` · 2026-08-05 기준 HEAD `8596ea0aa`
설계·측정·기각 사유는 `poc_numeric_sum_acc_results.md`, 여기는 **어디를 어떤 눈으로 볼 것인가**만 담는다.

## 0. 리뷰 범위

| | |
|---|---|
| 기준 커밋 | `c27d4754c` — CBRD-27150의 마지막 커밋. **그 뒤가 전부 POC** |
| 커밋 수 | 29 |
| 변경 | `src/` **14파일 / +1462 −9** |
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
⑤ 경로 연결      query_executor.c, px_scan_result_handler.cpp, stream_to_xasl.c
                  → 마킹을 직렬·병렬 양쪽에서, 스트림 복원 시 초기화
⑥ 분석 함수      query_analytic.cpp
                  → 같은 누산기를 비파괴 스냅샷으로
```

## 2. 파일별 진입점

줄 번호는 2026-08-05 HEAD 기준이며 변할 수 있다 — 함수명으로 찾는 것이 안전하다.

### ① 자료구조

| 파일 | 줄 | 무엇 |
|---|---|---|
| `xasl_aggregate.hpp` | `aggregate_accumulator` | `num_sum_acc`(워드 누산기) + `shared_from`(공유 인덱스, 1-based) |
| `xasl_analytic.hpp` | `analytic_list_node` | `num_sum_acc` — 분석 노드에도 같은 누산기 |
| `numeric_opfunc.h` | — | `NUMERIC_SUM_ACC`(14워드=254자리), `NUMERIC_POC_CHAIN_VAL`(u128 계수+scale+부호) |

### ② 산술 코어 — `numeric_opfunc.c` (+656 −9, 가장 큰 변경)

| 줄 | 함수 | 역할 |
|---|---|---|
| — | `numeric_init_poc_gate` | `__attribute__((constructor))` — 게이트·pow10 표를 **로드 시점 1회** |
| — | `numeric_poc_gate_enabled` | 게이트 조회 (분기 하나) |
| 4200 | `numeric_sum_acc_add_value` | **누적 진입** — 첫 값이면 누산기 초기화(memset+scale), 이후엔 더하기 |
| — | `numeric_sum_acc_add_core` | 워드 덧셈·뺄셈 (인트린식 캐리) |
| 4456 | `numeric_poc_chain_from_dbv` | DB_VALUE → 체인 값 (w[0]≠0이면 128비트 초과 → false) |
| 4486 | `numeric_poc_chain_mul` | 곱셈 — scale 상·하한 + `__builtin_mul_overflow` |
| 4527 | `numeric_poc_chain_add` | 덧·뺄셈 — scale 차 38 초과 거부, `__builtin_add_overflow` |
| 4590 | `numeric_poc_chain_to_dbv` | 체인 값 → DB_VALUE (**여기서만** 자릿수 계산) |
| 4618 | `numeric_sum_acc_snapshot` | 누산기 → 값, **누산기 보존** (분석 전용) |
| 4683 | `numeric_sum_acc_finalize` | 누산기 → 값, **누산기 종료** (집계 전용) |

### ③ 집계 실행 — `query_aggregate.cpp` (+415)

| 줄 | 지점 | 역할 |
|---|---|---|
| 129 | `qdata_numeric_sum_acc_enabled` | 게이트 |
| 763 | `qdata_agg_may_share_accumulator` | 공유 가능 판정 (SUM/AVG ∧ 비DISTINCT ∧ sort_list 없음 ∧ …) |
| 792 | `qdata_link_shared_accumulators` | **쿼리당 1회** 공유 링크 (`shared_from = owner_index + 1`) |
| 863 | 누적 스킵 | `shared_from > 0`이면 이 노드는 누적하지 않는다 |
| 904 | 집계 인자 체인 | operands가 **직접** `TYPE_INARITH`인 경우 (④와 다른 경로 — §3 주의) |
| 927 | 인자 peek | 단일 operand SUM/AVG는 딥카피 대신 참조 |
| 1597 | `qdata_propagate_shared_accumulators` | **값 확정(finalize) 전** 공유 상태 복사 |
| 1650 | `qdata_finalize_aggregate_list` | 그룹 마무리 — 첫 줄이 위 전파, 그 뒤 누산기 → 값 |
| 2513 / 2519 | `qdata_alloc_agg_hvalue` | 해시 그룹 배열 초기화 + `shared_from` 상속 |
| 2828 | `qdata_load_agg_hvalue_in_agg_list` | 해시값 → 집계 리스트로 `shared_from` 전달 |
| 2906 | 스필 저장 | 스필 경로의 공유 전파 (하한 검사 있음) |

### ④ 수식 평가 — `fetch.c` (+136), `regu_var.hpp` (+1)

| 줄 | 지점 | 역할 |
|---|---|---|
| 97 | `fetch_poc_chain_shape_ok` | 트리가 `{+,−,×}`·리프 단순값인지 — **쿼리당 1회**(마킹 시점) 호출 |
| 143 | `fetch_poc_eval_chain` | 트리를 재귀로 걸으며 체인 계산 (행마다) |
| 232 | `fetch_peek_arith` 안 훅 | 플래그 + 결과 도메인만 보고 진입 |
| `regu_var.hpp` | `REGU_VARIABLE_AGG_OPERAND = 0x2000` | 집계 인자로만 소비되는 수식 표시 |

### ⑤ 경로 연결

| 파일 | 줄 | 역할 |
|---|---|---|
| `query_executor.c` | 21225 `qexec_mark_aggregate_operand_expressions` | 게이트·shape 판정 후 플래그. **판정 근거는 집계 인자 `dbvalptr` == outptr `vfetch_to` 포인터 일치** |
| 〃 | 15943–15944 | **직렬** 경로 호출 (마킹 + 공유 링크) |
| `px_scan_result_handler.cpp` | 842–843 | **병렬 워커** 호출 — 이 2줄이 없으면 병렬에서 전부 무효 |
| `stream_to_xasl.c` | 5970–5971 | **CS 모드 결함 수정** — 스트림 복원 시 `shared_from = 0`, `is_active = false` |

### ⑥ 분석 함수 — `query_analytic.cpp` (+92)

| 줄 | 지점 | 역할 |
|---|---|---|
| 66 | `qdata_initialize_analytic_func` | `is_active = false` 초기화 |
| 154–166 | 인자 peek 조기 경로 | 누산기가 이미 돌면 peek해서 바로 누적 |
| 398–420 | 누적 본체 | 첫 값·NULL·비NUMERIC은 기존 경로 유지 |
| 1058–1060 | `snapshot` 호출 | 행마다 중간값을 내면서 누산기는 살려둔다 |

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

### 3-2. 헷갈리기 쉬운 지점

**체인 평가기가 두 개 있다.** 같은 일을 하는 중복이 아니라 진입 경로가 다르다:

| | `fetch.c: fetch_poc_eval_chain` | `query_aggregate.cpp: qdata_poc_eval_word_chain` |
|---|---|---|
| 언제 | 출력 수식(outptr)을 평가할 때 | 집계 노드 operands가 **직접** `TYPE_INARITH`일 때 |
| 리프 값 | `fetch_peek_dbval`로 그 자리에서 얻음 (`thread_p`·`vd`·`tpl` 필요) | 이미 평가된 `dbvalptr`/`dbval`만 읽음 |
| Q1에서 | **주 경로** (프로파일 5.1%) | 프로파일에 안 나타남 |

리뷰 시 확인할 것: 두 경로가 **같은 폴백 규칙**을 쓰는지, 한쪽만 고치고 다른 쪽을 놓치지 않았는지.

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

## 5. 아직 열려 있는 항목

| 항목 | 상태 |
|---|---|
| 코드 다듬기 | pow10 표 네이밍(`_gv_` 통일), `numeric_poc_digits_u128`과 기존 `float_numeric_get_decimal_digit` 중복 검토 |
| Jira 이슈화 | 40자리 초과 SUM 결과 변경(QA answer 갱신), 게이트 제거 |
| 기각·되돌림 완료 | 해시 델타 스킵(`5bec86046` → `623ebfe6e`) — 사유는 결과 문서 §4 |
