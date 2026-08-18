# 설계안 — NUMERIC SUM/AVG 워드 누산기 (이슈 생성 전 초안)

2026-07-31 · 대상: src/query/query_aggregate.cpp, src/xasl/xasl_aggregate.hpp, src/query/numeric_opfunc.c

## 문제

집계는 행마다 `qdata_add_dbval` → `float_numeric_db_value_add`를 호출하며, 매번 7단계를 전부 수행한다:
연산 버퍼 준비(memset ×3) → byte→word 변환 → scale 보정 → 덧셈 → 자릿수 계산 → overflow 체크 → 반올림 → DB_VALUE 생성.

콜그래프 실측 (SUM(b), 38자리 NUMERIC 100만 행) — float_numeric_db_value_add의 단계 순서대로,
각 단계의 담당 함수와 add 서브트리 내 비율:

| 단계 (소스 주석 번호) | 담당 함수 | 비율 |
|---|---|---|
| 검증·정밀도/scale 조회 | db_get_numeric_precision_and_scale | 1.0% |
| 1~2) scale 계산·버퍼 크기 결정 | 본체 인라인 (아래 self에 포함) | — |
| 3) 버퍼 초기화 | memset ×3 (VLA) | 7.4% |
| 3) byte→word 변환 ×2 | numeric_bytes_to_words | 2.3% |
| 4) scale 보정 | mul_normalize → mul_pow10 | 1.2% |
| **5) 덧셈** | **float_numeric_add** | **3.7%** |
| 6) 자릿수 재계산 | get_decimal_digit | 2.5% |
| 6) overflow 체크 | check_overflow_and_adjust_scale | 0.7% |
| **7) 반올림·pack** | **round_and_pack 9.0 + div_normalize 1.2 + div_pow10 3.5 + __udivti3 41.5** | **55.2%** |
| 8) 결과 DB_VALUE 생성 | db_make_numeric + db_value_domain_init | 2.8% |
| 본체 self (1~2단계 산술·부호 분기·루프 오버헤드) | float_numeric_db_value_add 자체 | 18.7% |
| 기타 (미해석 심볼 등) | | ~4.5% |

**누산기로 finalize에 미루는 6·7·8단계가 합쳐서 ~61%. 행당 유지되는 3(변환)~5(덧셈)는 ~7%.**
진짜 덧셈은 4% 미만이며, 절반 이상이 "매 행 40자리로 깎는 반올림"이다 — 성능 문제일 뿐 아니라 오차도 누적시킨다.
(주: 본체 self 18.7%는 컴파일러 인라인으로 여러 단계에 걸쳐 있어 심볼 단위로는 더 쪼갤 수 없음)

## 스코프

| 경로 | 처리 | 이유 |
|---|---|---|
| **query_aggregate의 PT_SUM/PT_AVG + NUMERIC 피연산자** | **워드 누산기 (신규, 1단계)** | 누계를 finalize에서만 읽음 → 5~7단계 지연 가능 |
| query_analytic — PARTITION BY만 있는 SUM/AVG | 후속 이슈 (2단계) | 파티션당 결과 1개 → 집계와 동일한 이득 구조 (finalize 횟수 = 결과값 개수). 같은 numeric_sum_acc_* API 재사용, 파티션 경계·값 배포 훅은 별도 파악 필요 |
| query_analytic — ORDER BY 누적합 | 후속 이슈 (2단계) | **일관성 사유**: SUM OVER의 마지막 행은 정의상 집계 SUM과 같아야 하므로 함께 바꿔야 함. 워드 상태는 정확하게 유지, 순서 그룹마다 내보낼 값 = round(정확 prefix 합) — 모든 running value가 드리프트 없는 올바른 반올림이 됨. 성능은 소폭 이득(덤) |

스코프 원칙: **"워드 상태는 항상 정확, 반올림은 값을 내보내는 시점의 스냅샷"** — 이 원칙 아래
집계 SUM과 분석 SUM OVER는 항상 서로 일치하며, 40자리 초과에서 어긋나는 것은
이항 연산 스펙(연산마다 반올림)을 따르는 수식 체인(a + b + …)뿐이다.
| STDDEV/VARIANCE 계열 | 기존 유지 | DOUBLE로 누적 (query_executor.c:21070, tp_Double_domain) — 원래 NUMERIC 덧셈 밖 |
| 표현식 덧셈 (a + b) | 기존 유지 | 1회성 연산, 연산별 반올림이 사용자 가시 스펙 |
| SUM/AVG + INT/BIGINT/DOUBLE/MONETARY | 기존 유지 | 이미 값싼 스칼라 덧셈 |

## 설계

### 상태 구조 — 포인터로, NUMERIC SUM/AVG일 때만 할당

```c
struct numeric_sum_accumulator
{
  uint64_t words[14];   /* 최대 254자리(TWICE_NUM_MAX_PREC) ≈ 14워드 */
  int used_words;       /* 실제 사용 중인 최상위 워드 수 */
  int scale;            /* 현재 누계의 scale */
  bool is_negative;
};
```

- `aggregate_accumulator`(xasl_aggregate.hpp:63)에 포인터 멤버로 추가. 해시 집계는 그룹 수 × 함수 수만큼
  누산기가 생기므로 112B를 모든 타입에 상시 인라인하지 않는다.
- **용량 14워드, 연산은 used_words만큼만.** 고정 NUMERIC(p,s)는 계수 최대 40자리 + log10(행수)라
  실사용 3~4워드. 14워드 전체가 필요한 경우는 Float NUMERIC에서 scale이 크게 다른 값이 섞일 때뿐.

### 행당 작업 (7단계 → 3단계)

1. bytes_to_words (17바이트 → 3워드)
2. scale 정렬 — 고정 컬럼이면 항상 동일해 no-op, Float NUMERIC만 가끔 mul_normalize (곱셈, 나눗셈 아님)
3. 부호 있는 워드 덧셈 — used_words+1 워드만 캐리 전파

used_words 추적 비용: 캐리 전파가 멈춘 위치를 기록하는 비교 1회뿐 (별도 스캔 없음).
현재 행마다 수행하는 get_decimal_digit 전체 워드 순회(실측 2.5%)를 대체하므로 오히려 감소.
뺄셈으로 상위 워드가 0이 되면 그 시점에 상위 0 워드만 확인해 내린다 (안 내려도 워드당 1 덧셈의 미미한 비용).

제거되는 것: memset ×3, 자릿수 계산, overflow 체크, 행당 반올림 나눗셈, round_and_pack, db_make_numeric.

### finalize — 5~7단계를 여기서 1회

위치: qdata_finalize_aggregate_list() (query_aggregate.cpp:1297)의 집계 함수별 마무리 루프에
SUM/AVG+NUMERIC 분기 추가 (COUNT_STAR 확정, AVG count 나누기가 이미 있는 그 루프 — 새 훅 불필요).
호출부: query_executor.c:15080 (GROUP BY 없음, 쿼리당 1회) / 19891 (그룹 닫힐 때, 그룹당 1회).

내용: get_decimal_digit → 40자리 초과 시 scale 조정(+오버플로 판정) → round_and_pack → db_make_numeric.
AVG는 그 뒤 count 나누기 (기존 그대로).

finalize가 그룹당 1회이므로 이득은 그룹당 행 수에 비례한다. 그룹당 행이 극히 적은 GROUP BY
(거의 유니크 키)에서는 이득이 작아지므로 측정 케이스에 포함한다.

### 불변식 — 반올림은 그룹당 정확히 1회 (finalize에서만)

부분합을 중간 경계(워커·spill)에서 반올림하면 행 분배나 메모리 상황에 따라 결과 끝자리가
달라질 수 있다(비결정성). 워드 상태는 경계에서 값 그대로 합치고, 반올림은 최종 finalize 1회만 수행한다.

### 해시 spill

위 불변식을 지키려면 spill 시 워드 상태를 무손실로 직렬화해야 한다 (부분합이 40자리를 넘을 수 있어
NUMERIC DB_VALUE로는 못 담음). 임시 리스트의 해당 컬럼 도메인을 바이너리(CHAR/VARBINARY 상당)로
잡아 words+scale+sign을 그대로 기록하는 방안을 우선 검토한다 — 임시 리스트는 내부 포맷이라
외부 호환성 문제 없음. 불가하면 spill 시 finalize로 후퇴하되, 그 경우 spill 발생 여부에 따라
결과가 달라질 수 있음을 이슈에 명시하고 합의한다.

### 병렬 병합

병렬 스캔의 병렬성은 워커별 누적에 있다: 각 워커가 클론된 XASL의 자기 agg_list에 독립 누적
(행당 이득이 워커 수만큼 병렬로 발생). `qdata_aggregate_accumulator_to_accumulator`는
워커 부분합을 원본에 합치는 직렬 병합으로, px_scan_result_handler.cpp의 write_finalize에서
writer_results_mutex 아래 호출된다 — 워커당 1회 수준이라 성능 무관, scale 정렬 + 워드 덧셈으로
정확성만 보장하면 된다. 위 불변식에 따라 워커 경계에서 finalize(반올림)하지 않고 워드 상태를
그대로 합친다 — 행 분배에 따라 결과가 달라지지 않게 하기 위함.

주의: XASL 클론 시 누산기 포인터 멤버를 워커마다 새로 할당해야 한다 (얕은 복사 시 워커 간 레이스).
해제는 클론 XASL을 정리하는 qexec_clear_xasl 경로에서 수행.

## API 배치 — 기존 함수는 분해하지 않고 새 진입점 추가

`float_numeric_db_value_add`는 이미 static 헬퍼(bytes_to_words, mul_normalize, add/sub/compare,
get_decimal_digit, check_overflow_and_adjust_scale, round_and_pack)의 조합이므로 분해가 불필요하다.
누산기는 같은 헬퍼들의 다른 조합으로 numeric_opfunc.c에 신규 함수 3개를 추가한다.

```c
int numeric_sum_acc_add_value (NUMERIC_SUM_ACC *acc, const DB_VALUE *val);   /* 행당: 변환+정렬+덧셈 */
int numeric_sum_acc_merge     (NUMERIC_SUM_ACC *acc, const NUMERIC_SUM_ACC *other);  /* 병렬 병합 */
int numeric_sum_acc_finalize  (NUMERIC_SUM_ACC *acc, DB_VALUE *result);      /* 5~7단계 1회 */
```

- float_numeric_db_value_add는 손대지 않는다 : 표현식 덧셈의 "연산마다 반올림"은 사용자 가시 스펙이고,
  핫 함수 리팩토링은 회귀 검증 범위만 키운다.
- query_aggregate.cpp는 불투명 포인터만 보유 — NUMERIC 내부 표현(워드 배열)은 numeric_opfunc.c에 캡슐화 유지.

## POC에서 확인된 함정 (본 구현 시 필수 반영)

해시 집계는 그룹의 첫 튜플만 정렬로 보내고 나머지는 부분합으로 저장했다가, 정렬 단계에서
부분합을 acc->value에 **로드한 뒤** 남은 튜플을 이어서 누적한다 (query_executor.c:5276,
qdata_load_agg_hvalue_in_agg_list). 따라서 누적 훅은 "워드 누산기가 비활성인데 curr_cnt ≥ 1"인
상태를 만나면 acc->value가 로드된 부분합임을 인지하고 **그 값을 워드 누산기에 먼저 시드**한 뒤
새 값을 더해야 한다. POC에서 이를 누락하여 해시 GROUP BY 결과가 "그룹 첫 값"으로 나오는 버그가
발생했고, 시드 규칙 추가로 해결했다. (그룹 첫 값(curr_cnt < 1)에서는 stale 워드 상태 강제 리셋도 필요)

**`numeric_words_to_bytes`는 최소 NUMERIC_AS_WORDS(3) 워드 윈도우를 요구한다**: 내부에서
`lsb_ptr = src + (src_words - NUMERIC_AS_WORDS)`로 LSW 기준 3워드를 역산하므로, 사용 워드가
3 미만이면 **버퍼 아래쪽을 읽는다**. acc->words처럼 전체가 0으로 초기화된 버퍼에서는 우연히 안전하지만,
스냅샷용 지역 배열(스택)에서는 쓰레기가 MSB(가중치 2^128)로 유입 — POC에서 AVG가 3.4e+38로 깨진
원인이었다. 스냅샷/finalize에서 변환 윈도우를 항상 `MAX(win, NUMERIC_AS_WORDS)`로 클램프할 것.

**경계·타입 방어 정책 (본구현 필수, 공통 패턴 = "물화 후 legacy 폴백")**:
- `win < NUMERIC_AS_WORDS`(3): 클램프가 정답 — 패딩 워드는 acc의 0-불변식 구간에서 복사되므로 안전
- `used_words == 14`는 합법 상태(254자리 = 843.8비트 = 정확히 14워드) — 스냅샷의 win 상한 클램프는
  데이터 유실 없음(+1은 여유 워드일 뿐). 최종 값의 표현 범위 초과는 스냅샷의
  `float_numeric_check_overflow_and_adjust_scale`이 이미 ER_IT_DATA_OVERFLOW로 검출
- **add의 최상위 캐리 유실 = 에러 (POC 반영 완료, `505cc63c3`)**: used_words=14에서 합이 15워드로
  넘치면(≥10^269.7) wraparound하므로, words_add가 마지막 캐리를 반환하고 캐리≠0이면
  ER_IT_DATA_OVERFLOW. 이 크기는 표현 한계(10^254)를 이미 넘어 as-is도 에러인 구간이라 오탐 없음
  (win<14이면 여유 워드가 캐리를 흡수하므로 캐리 유출 자체가 불가능)
- **scale 정렬 초과 = 폴백(에러 금지)**: 극단 scale 혼합(최대 −214~252 → 정렬 후 506자리)은 14워드
  초과 가능하지만 as-is는 VLA로 버퍼를 키워 성공하므로, 에러가 아니라 누산기 물화 후 해당 그룹은
  기존 행당 경로로 폴백 (고정 scale 컬럼은 정렬 자체가 no-op이라 성능 영향 없음)
- **타입 혼합 방어**: 피연산자 타입은 semantic check에서 공통 타입으로 확정(INT+NUMERIC→NUMERIC에
  CAST 부착, DOUBLE 혼합→DOUBLE로 훅 미발화)되어 실제로는 한 그룹 안에서 섞이지 않지만, 방어로
  ① 누산기 활성 중 비-NUMERIC 도착 → 물화 후 비활성화하고 legacy로, ② 시드 대상 acc->value가
  비-NUMERIC이면 활성화 포기하고 legacy로 (현재 POC는 ①에서 행 유실, ②에서 부분합 유실 가능)

**분석(analytic) 확장 시 시드 규칙 재사용**: 분석 SUM/AVG는 AVG 나눗셈으로부터 원본 합을 지키기 위해
part_value에 합을 보관했다가 value로 되돌리는(stash/reinstate) 구조라, 되돌린 직후의 누적 호출이
해시 집계의 "로드된 부분합"과 같은 상황이 된다. 동일한 시드 규칙(비활성 + 기존 value가 NUMERIC이면
먼저 시드)으로 해결되며, 스냅샷은 `qdata_finalize_analytic_func`의 is_same_group 분기 직전 1곳에만 둔다
(비파괴 스냅샷 — 러닝 SUM은 그룹이 이어지는 동안 계속 누적).

## POC 정확성 검증 결과 (20k 행 통제 데이터)

- 게이트 off/on 결과 완전 일치: SUM/AVG(대소값), 부호 혼합, NULL 혼합, DISTINCT,
  GROUP BY 7그룹(해시)·10만 그룹, 가변 scale Float NUMERIC(음수·10^-7~10^20 혼합)
- **40자리 초과 SUM에서만 차이가 나며, POC 결과가 정답**: Python 정수 연산으로 정확 합을 구해
  40자리 반올림한 값과 POC 결과가 완전 일치. 기존 경로는 행당 반올림 누적으로 소수 4째 자리부터 드리프트.
  (SUM(b) 20k행: 정답 …802.4691340 = POC, 기존 …802.4692299)

## 수정 지점 체크리스트

1. `qdata_aggregate_value_to_accumulator` — PT_SUM/PT_AVG에서 NUMERIC이면 워드 누산 분기
2. `qdata_aggregate_accumulator_to_accumulator` — 병렬/부분 집계 병합
3. finalize 지점 — 5~7단계 1회 수행
4. `qdata_save/load_agg_hentry_to_list` — spill 시 finalize 후 저장, 로드 후 재누적
5. `aggregate_accumulator` 구조체 — 포인터 멤버 추가, 할당/해제 (DISTINCT 경로도 같은 누적 함수 사용 확인)
6. XASL 클론 (병렬 스캔 워커별 agg_list) — 누산기 포인터를 클론마다 새로 할당, `qexec_clear_xasl`에서 해제

## 후속 설계 골격 — B2: 집계 인자 수식의 워드 체인 (POC 범위 밖, 8/3 논의)

집계 인자 수식 `price×(1−disc)×(1+tax)`를 DB_VALUE 왕복 없이 워드로 이어 계산:
- **add/sub**: 누산기 부품(scale 정렬 + 부호-크기 덧셈) 그대로 — 이미 검증된 코드
- **mul**: scale 보정 불필요(s = s1+s2), 계수는 워드 곱 (1워드끼리 = 128비트 곱 1개)
- **div**: 무한소수 → 결과 자릿수 정책이 본질적으로 필요한 유일한 연산 — **융합 제외, legacy 폴백**
  (AVG 나눗셈은 어차피 finalize 그룹당 1회)
- **연산별 반올림 스펙과의 양립**: 각 연산 후 clz 자릿수 상한 추적 — 상한 ≤ 40이면 legacy도
  반올림하지 않았을 케이스라 **비트 동일 보장**; 초과 가능 행만 legacy 폴백 (Q1류 실데이터 폴백 0%)
- 마지막 워드를 누산기에 직접 전달하는 진입점(`numeric_sum_acc_add_words(acc, words, n, scale, neg)`)
  → 체인 전체에서 DB_VALUE 소멸
- 가치: PG도 융합하지 않으므로(연산마다 init_var/make_result 지불) 이 조각에서 **PG 역전 가능** —
  행당 4연산 ~500ns → ~150ns급이면 V2 −15~20s 추정

기각 기록 — **A2 그룹키 캐시** (8/3): 인터리브된 그룹 값에서 미스 ~75% × 키 딥카피 비용으로 +20s
회귀, 철회. 재도전 조건: 복사-제로(백포인터) 형태 또는 클러스터 입력 한정.

## 합의 필요 (스펙 변경)

- **결과가 더 정확해진다**: 현재는 매 행 40자리로 반올림하며 오차가 누적됨. 변경 후엔 정확 누적 후
  마지막 1회 반올림 → 40자리 초과가 발생하는 SUM의 끝자리가 달라질 수 있음 (개선 방향).
  QA answer 파일 영향 검토 필요.
- **"SUM ≡ + 반복" 등식이 40자리 초과 구간에서 깨진다**: 현재는 SUM과 수식 체인(v1+v2+…)이
  같은 코드를 타서 항상 일치하지만, 변경 후 SUM은 마지막 1회만 반올림하므로 연산마다 반올림하는
  수식 체인과 끝자리가 다를 수 있다 (SUM 쪽이 정답에 더 가까움). SQL 표준은 SUM의 중간 정밀도를
  구현 정의로 두며(PostgreSQL numeric SUM은 정확 누적), 수식 체인의 연산별 반올림 스펙은 유지된다.
  이 일치성 자체를 스펙으로 요구할지가 채택의 핵심 결정 사항.
- **동일성 보장 조건 (정밀)**: 반올림은 누계 유효 자릿수 > 40(DB_MAX_NUMERIC_PRECISION)일 때만
  발생하므로, 스캔 순서상 모든 중간 누계가 40자리 이하이면 기존/신규 결과는 비트 단위로 동일하다.
  같은 부호만의 SUM은 중간 누계 ≤ 최종 합이므로 "최종 합 ≤ 40자리 ⇒ 동일"이 성립.
  부호 혼합 시에는 최종 합이 작아도 중간 누계가 40자리를 넘으면 달라질 수 있다.
  (컬럼 정밀도 기준이 아님에 주의 — NUMERIC(38) 값 100개면 누계가 40자리를 넘는다)
- **오버플로 판정 시점**: 스캔 중간 → finalize. 큰 양수·음수가 섞인 경우 현재는 중간에 터질 수 있던
  것이 안 터지게 됨 (개선이지만 동작 변경).

## 예상 효과 / 측정 계획

- add 서브트리에서 행당 비용의 ~85%가 제거 대상. SUM(b)(누계 40자리 초과) 문장 기준 30~40% 단축 추정.
- 누계가 40자리를 넘지 않는 SUM(s)은 나눗셈이 원래 없어 이득이 작음 — 두 케이스를 분리 측정.
- 측정: t_claude_num(i/s/b), 문장 elapsed 8회 최솟값 + perf 콜그래프로 add 서브트리 비율 전/후 비교.
  GROUP BY(그룹 다수) 케이스로 해시 spill 경로도 확인.
- 표시 방식: 누적 경로 내부의 함수별 점유율을 비중 내림차순으로 전/후 나란히 표로 제시한다.
  "무엇이 사라졌는지"가 표에서 바로 읽히도록 하기 위함 (예: __udivti3 41.5% → 0%).
  방법: perf record -g --call-graph dwarf → perf script로 스택을 접어, 누적 함수(before:
  float_numeric_db_value_add / after: numeric_sum_acc_add_value)가 스택에 포함된 샘플만
  leaf 심볼별로 집계. static 헬퍼는 인라인될 수 있으므로 심볼 단위 귀속임을 명시.

## 참고

- `float_numeric_db_value_add`의 memset 3개는 정적 분석 경고(빌드 실패)로 인해 존재 — 건드리지 않는다.
  (참고로 result_word는 add_fast가 word[0..1]을 쓰지 않아 memset이 논리적으로도 필수)
- 선행 작업: CBRD-27150 (128비트 나눗셈 호출 절감) — 본 설계의 실측 수치는 27150 적용 후 기준.
