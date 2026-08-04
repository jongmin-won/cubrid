# Q01 심층 분석 — TPCH-SSPQ FK 캠페인 (tpch-sspq-fk-r1-20260730)

레짐: `single-query-repeat WARM`, `single-connection-four-statements`, `configured node/gather-cap comparison`, `configured-equal buffer budget` (양쪽 8192MB). 정합성: `result-equivalent-at-SF10`, censored 아님.

## 1. 인과 배수 카드 (report §3-a)

```
R_wall 2.8070x [wall, 엔진당 3회 측정의 median]
= F_plan  1.0000x [plan-shape, 구조적 동일성]
× F_units 1.0197x [total-query-CPU/wall 보정, TWU로 설명]
× F_cpu   2.7407x [total query CPU-seconds]

F_cpu 2.7407x
= F_work 1.0000x [heap rows scanned = 59,986,052, 양 엔진 동일]
× F_cost 2.7407x [heap row당 total-query CPU-seconds]
```

재구성: `1.0000 × 1.01973 × 2.74071 = 2.79480` vs 헤드라인 `2.80695` → **잔차 0.433%**. 오차 예산: 블록 내 상대 sd는 CUBRID 0.266%, PostgreSQL 0.188% (quadrature 합성 0.326%). 추가로 CPU 분자를 공급한 stage-14.7 텔레메트리 런 자체가 각자 헤드라인 median보다 +0.177% (CUBRID) / +0.612% (PostgreSQL) 높았고, 이것만으로 잔차 대부분이 설명된다. 잔차는 오차 예산 안이며 **카드는 폐쇄(closed)** 되었다.

| Factor | 값 | 이벤트 단위 | 분모 | 공식 | Raw 근거 | 증거 유형 |
| --- | --- | --- | --- | --- | --- | --- |
| `F_plan` | 1.0000x | plan-node shape | n/a (anchor) | 구조적 동일성 (아래 §3) | `q1-plan-est-cubrid.out`, `q1-trace-cubrid.out`, `q1-plan-act-pg.out` | structural equality |
| `F_units` | 1.01973x | active execution units | CPU-seconds / wall-second | `U_P/U_C`, `U=CPU/T` | `Q01-*-telemetry.json` | profile attribution |
| `F_cpu` | 2.74071x | total query CPU-seconds | 쿼리 실행당 | `CPU_C/CPU_P` | `Q01-*-telemetry.json` | profile attribution |
| `F_work` | 1.0000x | heap rows scanned | rows | `W_C/W_P` = 59,986,052/59,986,052 | `q1-trace-cubrid.out`, `q1-plan-act-pg.out` | direct A/B |
| `F_cost` | 2.74071x | row당 CPU-seconds | rows scanned | `(CPU_C/W_C)/(CPU_P/W_P)` | `Q01-*-telemetry.json` | profile attribution |

`U_C = 186.67/31.248117 = 5.9738`, `U_P = 68.11/11.180813 = 6.0917`. 이중 계상 없음: 양 엔진이 동일한 행 집합을 스캔하므로 `F_work`는 정확히 1.0000이고, CPU 격차 전체가 `F_cost`에 실리며 plan이나 units로 재귀속되지 않는다.

`F_plan = 1.0000`은 가정이 아니라 구조적 동일성으로 입증: 양 엔진 모두 6-way parallel heap scan → partial hash aggregation → sorted/mergeable gather → final aggregate를, 동일한 스캔(59,986,052)/필터 통과(59,142,609, 제거 843,444)/출력(4행) 카디널리티 위에서 실행하며, 인덱스 접근도 스필도 없다 (CUBRID `GROUPBY ... page: 0, ioread: 0`; PostgreSQL `Batches: 1  Memory Usage: 32kB`, `Sort Method: quicksort  Memory: 26kB`).

## 2. 헤드라인 타이밍 (report §3-b)

| 항목 | CUBRID | PostgreSQL |
| --- | --- | --- |
| warmup (미집계) | 31.199999 s | 11.174569 s |
| measured run 1 | 31.192999 s | 11.143534 s |
| measured run 2 | 31.322999 s | 11.103748 s |
| measured run 3 | 31.167999 s | 11.112755 s |
| **median (헤드라인)** | **31.192999 s** | **11.112755 s** |
| mean | 31.227999 s | 11.120012 s |
| 블록 내 sd | 0.083217 s (0.266%) | 0.020862 s (0.188%) |
| sink bytes / SHA-256 | 6027 / `688ccadc9c738478…` | 2394 / `87ecd4a7c504b53b…` |

**Median wall ratio = 2.8070x (CUBRID / PostgreSQL).** 3개 값에서 신뢰구간은 주장하지 않는다.

WARM은 가정이 아니라 증명: 블록 전체 device `read_bytes` 델타 양쪽 0; warmup 대비 측정 런 편차 0.49% (CUBRID) / 0.64% (PostgreSQL); 버퍼 카운터 CUBRID `Num_data_page_lru3` 498,048 전후 안정, PostgreSQL `heap_blks_hit` +694,529/run, `heap_blks_read` +434,954/run; 런당 페이지 CUBRID 682,982 fetch, PostgreSQL 1,125,128 blocks(전체 heap). WARM 게이트 실패 없음.

### 2-b. 분석 함수 (`SUM/AVG OVER`) 대조 — 2026-08-04 추가

`t_lineitem` 216만 행(`l_shipdate <= 1992-04-01`), 병렬 6워커, 3회 최솟값.
**양쪽 모두 서버 모드**. PG는 `SET max_parallel_workers_per_gather = 6; SET work_mem = '256MB'`.

| 쿼리 | CUBRID (to-be) | PG | 배율 |
|---|---|---|---|
| `SUM(ep) OVER (PARTITION BY flag)` | 3.89s | 1.96s | 1.99x |
| `SUM(ep) OVER (ORDER BY shipdate)` | 3.92s | 2.06s | 1.90x |
| `AVG(qty) OVER (PARTITION BY flag, status)` | 4.31s | 2.14s | 2.01x |
| `SUM(ep×(1−disc)) OVER (ORDER BY shipdate)` | 4.17s | 2.52s | **1.65x** |

**읽는 법**: 집계는 1.44x인데 **분석은 1.65~2.0x로 격차가 더 크다.** 다만 그 격차는
NUMERIC 산술이 아니다 — 수식이 없는 2행과 수식이 있는 4행의 차이가 CUBRID는 **+0.25s**,
PG는 +0.46s다. 즉 CUBRID의 분석 경로에서 수식 평가는 이미 PG보다 싸고(누계산·peek가
적용됨), 남은 격차는 정렬·리스트파일 I/O 쪽이다. 집계 내부식 혼합을 분석까지 확장해도
기대 효과는 0.1s 수준이라 이 POC 범위에서는 값이 없다.

**미확정**: 분석 격차의 정체를 perf로 분해하지 못했다(서버 모드 결함 조사에 시간을 썼다).
정렬·리스트파일이라는 위 판단은 두 쿼리의 차분에서 나온 추론이며 프로파일 근거가 없다.

## 3. 플랜 비교 (report §4)

CUBRID 실측 (trace): `SELECT (time: 31877, fetch: 683074, ioread: 682957)` → `SCAN dba.lineitem (heap time: 31876, readrows: 59986052)` with `parallel workers: 6, heap time: 31595..31876, readrows: 9938887..10011920, gather: mergeable list` → `GROUPBY (time: 1, hash: partial, sort: true, page: 0, ioread: 0, rows: 4)`. 즉 6-way parallel scan → partial hash agg → mergeable gather.

PostgreSQL 실측 (`EXPLAIN ANALYZE BUFFERS VERBOSE TIMING`): `Finalize GroupAggregate` ← `Gather Merge` (Workers Planned 5, **Launched 5**) ← `Sort` (quicksort, 26 kB) ← `Partial HashAggregate` (Batches 1, 32 kB) ← `Parallel Seq Scan on lineitem` (`Filter: l_shipdate <= '1998-09-02'`, loops=6, `Buffers: shared hit=694494 read=430634`). Execution Time 11410.623 ms.

술어 동등성: 양쪽 모두 `l_shipdate <= 1998-09-02`로 귀결. 추정 품질: CUBRID card 58,072,645 vs 실제 통과 59,142,609 (−1.8%).

**경고 — 노드 타이머 귀속 규약이 엔진 간에 다르므로 그대로 교차 해석하면 안 된다.** PostgreSQL은 scan self-time ≈1.755 s, `Partial HashAggregate` self-time ≈9.50 s로 나누어 보고하지만, CUBRID는 `SCAN heap time 31876 ms`, `GROUPBY time 1 ms`로 보고한다. CUBRID에서는 집계가 parallel scan task *안에서* 실행되기 때문이다 (`qexec_hash_gby_agg_tuple`이 `parallel_scan::result_handler<...>::write`에서 호출됨, call graph로 확인) — 집계 비용이 스캔 노드에 접혀 들어간다. 귀속의 유효한 근거는 노드 타이머가 아니라 프로파일이다. 참고: CUBRID `SET OPTIMIZATION LEVEL 514` 덤프는 직렬 `sscan`만 렌더링하고 병렬 연산자를 표기하지 않는다 — plan-dump 한계이지 형상 차이가 아니다 (trace가 `parallel workers: 6`을 증명).

## 4. 프로파일 (report §6)

perf는 검증된 PID 집합에 부착 (all-CPU 아님). 커버리지: CUBRID 24,300 샘플 0 lost, `[unknown]` 0; PostgreSQL 9,552 샘플, `[unknown]` 0. task-clock 교차검증: CUBRID 6.038 CPUs utilized vs TWU 5.9774; PostgreSQL 6.232 vs TWU 6.1209.

| 지표 | CUBRID | PostgreSQL | 비 |
| --- | --- | --- | --- |
| **IPC** | **2.527** | **2.354** | 1.073 (CUBRID 우위) |
| **행당 명령어 수** | **21,279** | **7,216** | **2.949x** |
| **행당 사이클** | **8,422** | **3,065** | **2.747x** |

CUBRID top self cost: `float_numeric_db_value_add` 9.72%, `heap_attrinfo_read_dbvalues` 6.06%, `float_numeric_db_value_mul` 5.67%, `qdata_add_dbval` 4.56%, `fetch_peek_arith` 3.63%, `pr_clear_value` 3.55%, `qdata_evaluate_aggregate_list` 3.41%, `tp_value_cast_internal` 2.96%, `db_value_domain_init` 2.88%, `float_numeric_db_value_sub` 2.82%, `qexec_hash_gby_agg_tuple` 2.54%, `mr_data_readval_numeric` 2.30%, `malloc` 2.06%, `_int_free` 1.87%.

PostgreSQL top self cost: `ExecInterpExpr` 13.79%, `init_var_from_num` 8.04%, `detoast_attr` 7.40%, `AllocSetAlloc` 7.24%, `tts_buffer_heap_getsomeattrs` 6.46%, `make_result_safe` 5.65%, `mul_var` 4.46%, `accum_sum_add` 3.21%, `AllocSetFree` 2.26%, `do_numeric_accum` 2.25%, `strip_var` 2.15%, `sub_abs` 2.11%, `palloc` 2.01%, `numeric_mul_safe` 1.97%.

밴드 분석 (CUBRID top-40 self): expression/NUMERIC/DB_VALUE **62.35%**, alloc/memops 5.22%, buffer-fetch+tuple-deform 6.78% (그중 진짜 page-fetch 경로는 1% 미만: `pgbuf_get_vpid_ptr` 0.19%, `pgbuf_unlatch_void_zone_bcb` 0.06%, `pgbuf_get_victim` 0.06%, 커널 `filemap_get_read_batch` 0.13% + `filemap_read` 0.07%). PostgreSQL의 대응 expression/numeric 밴드는 ~59%. **결론: 격차는 스톨이 아니라 명령어 수다** — CUBRID는 행당 2.949x의 명령어를 1.073x *더 좋은* IPC로 리타이어하여 행당 사이클 2.747x가 되며, 이는 `F_cpu = 2.7407`과 일치한다.

지배 심볼의 검증된 호출 경로: `float_numeric_db_value_add` ← `qdata_add_numeric_to_dbval` ← `qdata_add_dbval` ← `qdata_aggregate_value_to_accumulator` ← `qdata_evaluate_aggregate_list` ← `qexec_hash_gby_agg_tuple` ← `parallel_scan::result_handler<1>::write` ← `parallel_scan::task<1,0>::loop`.

## 5. 소스 대조 (report §7) — PostgreSQL은 이 문제를 어떻게 푸는가

| 항목 | CUBRID file:line | PostgreSQL file:line | 무엇이 다른가 | 분류 |
| --- | --- | --- | --- | --- |
| 행당 NUMERIC 합 누산 | `src/query/numeric_opfunc.c:2477` `float_numeric_db_value_add()`; 도달 경로 `src/query/query_opfunc.c:2059` `qdata_add_numeric_to_dbval()` | `src/backend/utils/adt/numeric.c:4821` `do_numeric_accum()` → `accum_sum_add()` (선언 `:603`), `NumericSumAccum` (`:381`) | CUBRID는 매 행마다 완전 일반화된 연산자를 실행: 인자 타입/NULL 검증, 양쪽 피연산자 `db_get_numeric_precision_and_scale`, 결과 정밀도/스케일 계산, `uint64_t[calc_words]` VLA 버퍼 3개 `memset`, 양 피연산자의 `numeric_bytes_to_words` 변환(`BSWAP64` 포함), 필요시 `float_numeric_mul_normalize` 재스케일, 덧셈, `float_numeric_get_decimal_digit` 정밀도 재계산, `float_numeric_check_overflow_and_adjust_scale`, `float_numeric_round_and_pack`, `db_make_numeric`으로 DB_VALUE 생성. PostgreSQL의 행당 작업은 32-bit digit 배열에 대한 `accum_digits[i] += (int32) val_digits[val_i]`가 전부 — 양수/음수 버퍼 분리, 캐리 전파는 `num_uncarried == NBASE-1`일 때만, 할당 없음, 정밀도 재계산 없음, 반올림 없음; 정규화는 `accum_sum_final()`에서 1회 | same stage, lower measured cost |
| 집계 결과 수명주기 | `src/query/numeric_opfunc.c:2477` (행마다 `db_make_numeric`) + 프로파일의 `pr_clear_value` 3.55%, `db_value_domain_init` 2.88%, `malloc`+`_int_free` 3.93% | `numeric.c` `accum_sum_add()`가 사전 크기 확보된 누산기에 제자리(in-place) 기록; `AllocSetAlloc`/`palloc` 비용은 컨텍스트 풀링됨 | CUBRID는 행당·집계당 DB_VALUE를 생성하고 해제하여 핫 루프에 할당기 churn을 만든다; PostgreSQL은 영속 누산기를 변경(mutate)한다 | same stage, lower measured cost |
| 대형 순차 스캔의 스캔 저항성 버퍼 교체 | `src/storage/page_buffer.c` (LRU 존/희생자 선택; `pgbuf_get_victim`, `pgbuf_unlatch_void_zone_bcb` 프로파일에서 관측) | `src/backend/storage/buffer/freelist.c` (`BufferAccessStrategy`, `BAS_BULKREAD` ring) | CUBRID의 교체 정책은 작업셋이 풀을 초과하는 스캔이 자기 상주 페이지를 스스로 축출하게 하여 재사용률 0.005%; PostgreSQL의 bulk-read ring은 상주 집합을 보호하여 61.7% 유지 | structural absence |
| 튜플 deform / detoast | `heap_attrinfo_read_dbvalues` 6.06%, `mr_data_readval_numeric` 2.30% | `tts_buffer_heap_getsomeattrs` 6.46%, `detoast_attr` 7.40%, `init_var_from_num` 8.04% | 양쪽 모두 비례적으로 유사한 비용; CUBRID 고유 결함 아님 | common to both engines |

3행의 부재(absence) 주장 검증: 고정된 CUBRID 트리 `/home/cubrid/dev/tpch-sspq-fk-r1/cubrid-src`의 `src/`에서 `--include=*.c --include=*.cpp --include=*.h --include=*.hpp`로 `BufferAccessStrategy`, `BAS_BULKREAD`, `bulk.*read.*ring`, `ring buffer`, `scan.*resistant`를 `grep -rn` — 대응물 없음, `src/storage/page_buffer.c`의 일반 LRU 존 처리만 존재. 이 WARM 레짐에서 추가가 이득이라는 주장은 하지 않는다 (IMP-002의 상한 참조).

## 6. 인과 분해 서사 (report §8) — 배제된 설명과 그 근거 수치

1. **플랜은 원인이 아니다.** `F_plan = 1.0000` — 구조적 동일성: 동일한 6-way parallel scan → partial hash agg → merge gather → final agg 형상, 동일 카디널리티, 양쪽 모두 스필 없음, 인덱스 없음.
2. **병렬성은 원인이 아니다.** `F_units = 1.0197`. 양쪽 모두 실제 6 units 실행: CUBRID trace `parallel workers: 6`, TWU 5.9774, perf 6.038 CPUs; PostgreSQL `Workers Launched: 5` + leader, TWU 6.1209, perf 6.232. PostgreSQL의 2% 미만 활용도 우위는 io worker(2.27 core-s)의 스캔 중첩에서 오고, serial tail은 0.117 s vs 0.122 s로 동등.
3. **작업량은 원인이 아니다.** `F_work = 1.0000` 정확히 — 59,986,052행 스캔, 59,142,609행 통과, 4행 출력, 양쪽 동일.
4. **행당 CPU 비용이 원인이다.** `F_cost = 2.7407`: 3.1119 µs/row vs 1.1354 µs/row. perf 분해로 행당 명령어 2.949x × (1/1.073 IPC) → 행당 사이클 2.747x, `F_cost`와 0.3% 이내 일치. IPC가 아니라 명령어 수가 격차를 실어 나르므로, 원인은 메모리 스톨·캐시·주파수가 아니라 *행당 실행되는 작업의 양*이다.
5. **국소화.** CUBRID 프로파일 self cost의 62.35%가 expression/NUMERIC/DB_VALUE 밴드에 있고 PostgreSQL도 ~59%로 같은 밴드에 비슷한 *비율*을 쓴다 — 즉 CUBRID의 초과분은 PostgreSQL이 회피하는 다른 밴드가 아니라 같은 밴드 안에 농축되어 있다. 최대 단일 심볼 `float_numeric_db_value_add`(9.72%)는 오직 집계 누산 경로에서만 도달하며, 소스 대조는 PostgreSQL이 같은 논리 단계를 deferred-carry int32 누산기로 수행함을 보인다. → IMP-001.
6. **명시적으로 주장하지 않는 것.** 0.005% 버퍼 적중률은 실재하지만 여기서는 CPU의 1% 미만 비용이다 — device `read_bytes`가 0이고 OS 페이지 캐시가 모든 미스를 흡수하기 때문. 이는 상한이 명시된 IMP-002로 등재되며 `F_cost`에 수치로 합산되지 않는다. 저장 크기 비대칭(heap 10.42 GiB vs 8.58 GiB)도 기록만 하고 팩터로 변환하지 않는다 — `F_work`가 행 기준으로 정의되어 바이트 차이를 넣으면 같은 스캔을 이중 계상하게 된다.

## 7. 개선 후보 (report §9)

| ID | 근본 원인 | 상태 | 증거 유형 | 효과 |
| --- | --- | --- | --- | --- |
| IMP-001 | 집계 누산이 deferred-carry 고속 합산 누산기 대신 완전 일반화된 행당 NUMERIC add/mul을 수행 | `measured` | profile attribution | 2.74x 총 쿼리 CPU 격차의 대부분; 62.35% expression/NUMERIC/DB_VALUE 밴드로 상한 |
| IMP-002 | 작업셋이 풀을 근소 초과하는 반복 스캔에서 버퍼 교체가 ~0% 재사용을 초래 | `observed` | upper bound | 이 WARM 레짐에서 **쿼리 CPU의 1% 미만** (device `read_bytes` = 0) |

효과는 합산하지 않는다: 두 후보는 서로소 코드 경로(집계 누산 vs 페이지 교체)를 다루며, IMP-002의 수치는 `F_cost` 지분이 아니라 의도적으로 상한(upper bound)으로 기술된다. 어느 쪽도 `validated` 아님 — 수정에 대한 정합성 증거가 아직 없다.

이 페이지는 Git report(commit c73e858237b6197060105af2ed4a0b92534f373d, tpch-sspq/reports/Q01/report.md)의 미러다.