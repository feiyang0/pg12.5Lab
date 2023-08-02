#include "postgres.h"

#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/table.h"
#include "access/transam.h"
#include "access/xact.h"
#include "access/xlog.h"
#include "access/xlog.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "storage/proc.h"
#include "storage/procarray.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"

extern int					txn_id;
int					txn_id;


extern void _PG_init(void);
void _PG_init(void)
{
    DefineCustomIntVariable("txn_id", "Sets the dirtyread txn_id",
                            "Valid range is 1...maxtxn", &txn_id,
                            0, 0, INT_MAX, PGC_USERSET, 0, NULL, NULL, NULL);

}

Datum dirtyread(PG_FUNCTION_ARGS);

typedef struct dirtyread_ctx_state
{
    Relation		rel;
    TupleDesc		desc;
    TableScanDesc	scan;
} dirtyread_ctx_state;

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(dirtyread);
Datum dirtyread(PG_FUNCTION_ARGS)
{
    FuncCallContext *funcctx;
    dirtyread_ctx_state *inter_call_data = NULL;
    HeapTuple tuple;

    if (SRF_IS_FIRSTCALL())
    {
        MemoryContext oldcontext;
        Oid relid;
        TupleDesc tupdesc;

        relid = PG_GETARG_OID(0);
        funcctx = SRF_FIRSTCALL_INIT();
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

        inter_call_data = (dirtyread_ctx_state *)palloc(sizeof(dirtyread_ctx_state));
        inter_call_data->rel = table_open(relid, AccessShareLock);
        inter_call_data->desc = RelationGetDescr(inter_call_data->rel);

        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            elog(ERROR, "return type must be a row type");
        funcctx->tuple_desc = BlessTupleDesc(tupdesc);

        inter_call_data->scan = heap_beginscan(inter_call_data->rel, SnapshotAny, 0, NULL, NULL, 0);

        funcctx->user_fctx = (void *)inter_call_data;

        MemoryContextSwitchTo(oldcontext);
    }

    funcctx = SRF_PERCALL_SETUP();
    inter_call_data = (dirtyread_ctx_state *)funcctx->user_fctx;

    // txn_id没设置值时输出page内容
    if (txn_id == 0) {
        if ((tuple = heap_getnext(inter_call_data->scan, ForwardScanDirection)) != NULL)
        {
            SRF_RETURN_NEXT(funcctx, heap_copy_tuple_as_datum(tuple, inter_call_data->desc));
        }
        else
        {
            heap_endscan(inter_call_data->scan);
            table_close(inter_call_data->rel, AccessShareLock);
            SRF_RETURN_DONE(funcctx);
        }
    }
    while ((tuple = heap_getnext(inter_call_data->scan, ForwardScanDirection)) != NULL)
    {
        TransactionId tMin = tuple->t_data->t_choice.t_heap.t_xmin;
        TransactionId tMax = tuple->t_data->t_choice.t_heap.t_xmax;
        bool tMaNormal = TransactionIdIsNormal(tMax);
        bool tMiNormal = TransactionIdIsNormal(tMin);
        bool maCommit = true, commit;
        bool miCommit = TransactionIdDidCommit(tMin);

        if (tMaNormal) maCommit = TransactionIdDidCommit(tMax);
        commit = maCommit & miCommit;

        /* elog(NOTICE, "tuple_min: %d, tuple_max: %d commit:%d",
                       tMin, tMax, commit); */

        // 对于not commit的情况, tMin或tMax作废
        if (!commit) {
            // 插入失败的情况：tMin作废，[-无穷大, 0),tMin任何值不可见
            // 删除失败的情况：tMax作废, 可见区间变为变为[mi,+无穷大）
            // 两种情况都符合，输出结果
            if (tMiNormal && tMaNormal && tMin <= txn_id ) {
                SRF_RETURN_NEXT(funcctx, heap_copy_tuple_as_datum(tuple, inter_call_data->desc));
            }else // 其他情况跳过
                continue;
        }

        // 对于commit的tuple
        // 插入的情况tMax为0: txn_id ∈ [mi, +无穷大)
        // 删除的情况tMax!=0: txn_id ∈ [mi, ma), 记录可见
        if (tMiNormal && tMin <= txn_id && ( tMax == 0 || ( tMaNormal &&  txn_id < tMax  )) ) {
            SRF_RETURN_NEXT(funcctx, heap_copy_tuple_as_datum(tuple, inter_call_data->desc));
        }else {
            continue;
        }
    }
    // 没有多余tuple返回
    heap_endscan(inter_call_data->scan);
    table_close(inter_call_data->rel, AccessShareLock);
    SRF_RETURN_DONE(funcctx);

}