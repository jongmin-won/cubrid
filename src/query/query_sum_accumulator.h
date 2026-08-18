/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * query_sum_accumulator.h - running-sum accumulator for aggregate and
 * analytic SUM/AVG (POC)
 *
 * Only the container lives here, so that the XASL accumulator structs can
 * embed it without pulling in an arithmetic package.  The word-mode
 * arithmetic stays in numeric_opfunc.c (numeric_sum_acc_*) and the type
 * dispatch and typed modes stay in query_opfunc.c (qdata_sum_acc_*).
 */

#ifndef _QUERY_SUM_ACCUMULATOR_H_
#define _QUERY_SUM_ACCUMULATOR_H_

#ident "$Id$"

#include "dbtype_def.h"

#include <stdint.h>

/*
 * SUM_ACC: accumulator for aggregate/analytic SUM/AVG (POC).
 *
 * For NUMERIC input it accumulates raw words and defers digit counting,
 * overflow checking, rounding and DB_VALUE packing to a single finalize
 * call, instead of performing them for every accumulated row.
 *
 * Invariant: rounding happens exactly once per group, at finalize.
 *
 * For SHORT/INTEGER/BIGINT/DOUBLE input the same accumulator runs in a typed
 * mode instead: the running sum lives in int_sum or dbl_sum and `sum_type` records
 * which input type it stands for.  The legacy path accumulates in the *input*
 * type -- SUM (SHORT) overflows past 32767 -- so every typed add re-checks the
 * input type's range and raises the same ER_QPROC_OVERFLOW_ADDITION at the
 * same row the legacy path would.  Integer adds are exact and double adds are
 * the same IEEE operations in the same order, so the results are bit-identical;
 * the saving is the per-row DB_VALUE dispatch, not the arithmetic.
 *
 * FLOAT input runs as sum_type DOUBLE: the legacy accumulation domain for a FLOAT
 * argument is DOUBLE (a sum may pass FLT_MAX mid-group and come back without an
 * error; only the final demotion to FLOAT can raise ER_IT_DATA_OVERFLOW), so
 * double accumulation is the faithful reproduction, not float-by-float.
 *
 * `sum_type` holds the DB_TYPE the sum accumulates under -- DB_TYPE_NUMERIC for
 * the word mode, the typed types above otherwise -- and is meaningful only
 * while is_active is set: activation always writes it, and nothing reads it
 * while the accumulator is inactive.
 */
#define SUM_ACC_NUMERIC_WORDS  (14)	/* covers TWICE_NUM_MAX_PREC (256) decimal digits */

typedef struct sum_acc SUM_ACC;
struct sum_acc
{
  /* SHORT/INTEGER/BIGINT type */
  int64_t int_sum;
  /* FLOAT/DOUBLE type */
  double dbl_sum;

  /* NUMERIC type */
  uint64_t words[SUM_ACC_NUMERIC_WORDS];	/* sign-magnitude coefficient; big-endian, last word is the LSW */
  int used_words;		/* active low words */
  int scale;
  bool is_negative;		/* the typed sums carry their own sign */

  /* common */
  bool is_active;		/* false until the first value */
  DB_TYPE sum_type;		/* accumulation domain; valid only while is_active */
};

/* The two tests below are the accumulator's own contract -- what the box takes and
 * under which mode -- and the accumulator core (qdata_sum_acc_start/add_dbv/
 * accumulate) uses them directly.  The aggregate and analytic ENTRY policies wrap
 * them under their own names further down; entry code tests those, never these. */

/* the input types the accumulator takes; everything else stays legacy */
#define SUM_ACC_IS_SUPPORTED_TYPE(t) \
  ((t) == DB_TYPE_NUMERIC || (t) == DB_TYPE_INTEGER || (t) == DB_TYPE_BIGINT \
   || (t) == DB_TYPE_SHORT || (t) == DB_TYPE_DOUBLE || (t) == DB_TYPE_FLOAT)

/* the sum_type an input type accumulates under: NUMERIC keeps the word mode, FLOAT
 * widens to DOUBLE (the legacy accumulation domain), the other typed inputs
 * accumulate as themselves; DB_TYPE_NULL = not an accepted input */
static inline DB_TYPE
sum_acc_sum_type_for (DB_TYPE t)
{
  switch (t)
    {
    case DB_TYPE_NUMERIC:
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_DOUBLE:
      return t;
    case DB_TYPE_FLOAT:
      return DB_TYPE_DOUBLE;
    default:
      return DB_TYPE_NULL;
    }
}

/* aggregate entry policy: the aggregate path takes everything the accumulator
 * takes, so these are the contract under its own name */
#define SUM_ACC_IS_AGG_SUPPORTED_TYPE(t) SUM_ACC_IS_SUPPORTED_TYPE (t)

static inline DB_TYPE
sum_acc_agg_sum_type_for (DB_TYPE t)
{
  return sum_acc_sum_type_for (t);
}

/* analytic entry policy: FLOAT is refused even though the accumulator itself
 * takes it -- the analytic legacy accumulates in the result domain, so a FLOAT
 * argument really is summed float-by-float (rounded to float after every add,
 * overflowing at FLT_MAX mid-partition), and double accumulation cannot
 * reproduce that.  Spelled out standalone: layering a FLOAT override on top of
 * the contract map leaves an extra compare in the generated code. */
#define SUM_ACC_IS_ANALYTIC_SUPPORTED_TYPE(t) \
  ((t) == DB_TYPE_NUMERIC || (t) == DB_TYPE_INTEGER || (t) == DB_TYPE_BIGINT \
   || (t) == DB_TYPE_SHORT || (t) == DB_TYPE_DOUBLE)

static inline DB_TYPE
sum_acc_analytic_sum_type_for (DB_TYPE t)
{
  switch (t)
    {
    case DB_TYPE_NUMERIC:
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_DOUBLE:
      return t;
    default:
      return DB_TYPE_NULL;
    }
}

#endif /* _QUERY_SUM_ACCUMULATOR_H_ */
