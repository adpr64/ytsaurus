#include "evaluation_helpers.h"
#include "column_evaluator.h"
#include "private.h"
#include "helpers.h"
#include "plan_fragment.h"
#include "plan_helpers.h"
#include "query_statistics.h"

#include <yt/ytlib/table_client/pipe.h>
#include <yt/ytlib/table_client/schemaful_reader.h>

#include <yt/core/concurrency/scheduler.h>

#include <yt/core/profiling/scoped_timer.h>

namespace NYT {
namespace NQueryClient {

using namespace NConcurrency;
using namespace NTableClient;

static const auto& Logger = QueryClientLogger;

////////////////////////////////////////////////////////////////////////////////

const i64 PoolChunkSize = 32 * 1024;
const i64 BufferLimit = 32 * PoolChunkSize;

TTopCollector::TTopCollector(i64 limit, TComparerFunction* comparer)
    : Comparer_(comparer)
{
    Rows_.reserve(limit);
}

std::pair<TMutableRow, int> TTopCollector::Capture(TRow row)
{
    if (EmptyBufferIds_.empty()) {
        if (GarbageMemorySize_ > TotalMemorySize_ / 2) {
            // Collect garbage.

            std::vector<std::vector<size_t>> buffersToRows(Buffers_.size());
            for (size_t rowId = 0; rowId < Rows_.size(); ++rowId) {
                buffersToRows[Rows_[rowId].second].push_back(rowId);
            }

            auto buffer = New<TRowBuffer>(PoolChunkSize);

            TotalMemorySize_ = 0;
            AllocatedMemorySize_ = 0;
            GarbageMemorySize_ = 0;

            for (size_t bufferId = 0; bufferId < buffersToRows.size(); ++bufferId) {
                for (auto rowId : buffersToRows[bufferId]) {
                    auto& row = Rows_[rowId].first;

                    auto savedSize = buffer->GetSize();
                    row = buffer->Capture(row);
                    AllocatedMemorySize_ += buffer->GetSize() - savedSize;
                }

                TotalMemorySize_ += buffer->GetCapacity();

                if (buffer->GetSize() < BufferLimit) {
                    EmptyBufferIds_.push_back(bufferId);
                }

                std::swap(buffer, Buffers_[bufferId]);
                buffer->Clear();
            }
        } else {
            // Allocate buffer and add to emptyBufferIds.
            EmptyBufferIds_.push_back(Buffers_.size());
            Buffers_.push_back(New<TRowBuffer>(PoolChunkSize));
        }
    }

    YCHECK(!EmptyBufferIds_.empty());

    auto bufferId = EmptyBufferIds_.back();
    auto buffer = Buffers_[bufferId];

    auto savedSize = buffer->GetSize();
    auto savedCapacity = buffer->GetCapacity();

    auto capturedRow = buffer->Capture(row);

    AllocatedMemorySize_ += buffer->GetSize() - savedSize;
    TotalMemorySize_ += buffer->GetCapacity() - savedCapacity;

    if (buffer->GetSize() >= BufferLimit) {
        EmptyBufferIds_.pop_back();
    }

    return std::make_pair(capturedRow, bufferId);
}

void TTopCollector::AccountGarbage(TRow row)
{
    GarbageMemorySize_ += GetUnversionedRowByteSize(row.GetCount());
    for (int index = 0; index < row.GetCount(); ++index) {
        const auto& value = row[index];

        if (IsStringLikeType(EValueType(value.Type))) {
            GarbageMemorySize_ += value.Length;
        }
    }
}

void TTopCollector::AddRow(TRow row)
{
    if (Rows_.size() < Rows_.capacity()) {
        auto capturedRow = Capture(row);
        Rows_.emplace_back(capturedRow);
        std::push_heap(Rows_.begin(), Rows_.end(), Comparer_);
    } else if (!Comparer_(Rows_.front().first, row)) {
        auto capturedRow = Capture(row);
        std::pop_heap(Rows_.begin(), Rows_.end(), Comparer_);
        AccountGarbage(Rows_.back().first);
        Rows_.back() = capturedRow;
        std::push_heap(Rows_.begin(), Rows_.end(), Comparer_);
    }
}

std::vector<TMutableRow> TTopCollector::GetRows(int rowSize) const
{
    std::vector<TMutableRow> result;
    result.reserve(Rows_.size());
    for (const auto& pair : Rows_) {
        result.push_back(pair.first);
    }
    std::sort(result.begin(), result.end(), Comparer_);
    for (auto& row : result) {
        row.SetCount(rowSize);
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

TJoinEvaluator GetJoinEvaluator(
    const TJoinClause& joinClause,
    TConstExpressionPtr predicate,
    const TTableSchema& selfTableSchema)
{
    const auto& equations = joinClause.Equations;
    auto isLeft = joinClause.IsLeft;
    auto canUseSourceRanges = joinClause.CanUseSourceRanges;
    auto keyPrefix = joinClause.KeyPrefix;
    const auto& equationByIndex = joinClause.EquationByIndex;
    auto& foreignTableSchema = joinClause.ForeignTableSchema;
    auto& foreignKeyColumnsCount = joinClause.ForeignKeyColumnsCount;
    auto& renamedTableSchema = joinClause.RenamedTableSchema;
    auto& joinedTableSchema = joinClause.JoinedTableSchema;
    auto& foreignDataId = joinClause.ForeignDataId;
    auto foreignPredicate = ExtractPredicateForColumnSubset(predicate, renamedTableSchema);

    // Create subquery TQuery{ForeignDataSplit, foreign predicate and (join columns) in (keys)}.
    auto subquery = New<TQuery>(std::numeric_limits<i64>::max(), std::numeric_limits<i64>::max());

    subquery->TableSchema = foreignTableSchema;
    subquery->KeyColumnsCount = foreignKeyColumnsCount;
    subquery->RenamedTableSchema = renamedTableSchema;
    subquery->WhereClause = foreignPredicate;

    // (join key... , other columns...)
    auto projectClause = New<TProjectClause>();
    std::vector<TConstExpressionPtr> joinKeyExprs;

    if (canUseSourceRanges) {
        int lookupKeySize = keyPrefix;
        for (int column = 0; column < lookupKeySize; ++column) {
            int index = equationByIndex[column];
            if (index >= 0) {
                const auto& equation = equations[index];
                projectClause->AddProjection(equation.second, InferName(equation.second));
            } else {
                const auto& evaluatedColumn = renamedTableSchema.Columns()[column];
                auto referenceExpr = New<TReferenceExpression>(evaluatedColumn.Type, evaluatedColumn.Name);
                projectClause->AddProjection(referenceExpr, InferName(referenceExpr));
            }
        }

    } else {
        for (const auto& column : equations) {
            projectClause->AddProjection(column.second, InferName(column.second));
            joinKeyExprs.push_back(column.second);
        }
    }

    for (const auto& column : renamedTableSchema.Columns()) {
        if (projectClause->ProjectTableSchema.FindColumn(column.Name)) {
            continue;
        }
        projectClause->AddProjection(New<TReferenceExpression>(
            column.Type,
            column.Name),
            column.Name);
    }

    subquery->ProjectClause = projectClause;

    auto subqueryTableSchema = subquery->GetTableSchema();

    std::vector<std::pair<bool, int>> columnMapping;
    for (const auto& column : joinedTableSchema.Columns()) {
        if (auto self = selfTableSchema.FindColumn(column.Name)) {
            columnMapping.emplace_back(true, selfTableSchema.GetColumnIndex(*self));
        } else if (auto foreign = subqueryTableSchema.FindColumn(column.Name)) {
            columnMapping.emplace_back(false, subqueryTableSchema.GetColumnIndex(*foreign));
        } else {
            YUNREACHABLE();
        }
    }

    // self to joined indexes;
    // foreign to joined indexes;
    std::vector<size_t> selfToJoinedMapping;
    std::vector<size_t> foreignToJoinedMapping;

    return [=] (
        TExecutionContext* context,
        THasherFunction* groupHasher,
        TComparerFunction* groupComparer,
        TJoinLookup& joinLookup,
        std::vector<TRow> keys,
        std::vector<std::pair<TRow, i64>> chainedRows,
        TRowBufferPtr permanentBuffer,
        void** consumeRowsClosure,
        void (*consumeRows)(void** closure, TRow* rows, i64 size))
    {
        // TODO: keys should be joined with allRows: [(key, sourceRow)]
        TRowRanges ranges;

        if (canUseSourceRanges) {
            LOG_DEBUG("Using join via source ranges");
            for (auto key : keys) {
                auto lowerBound = key;

                auto upperBound = TMutableRow::Allocate(permanentBuffer->GetPool(), keyPrefix + 1);
                for (int column = 0; column < keyPrefix; ++column) {
                    upperBound[column] = lowerBound[column];
                }

                upperBound[keyPrefix] = MakeUnversionedSentinelValue(EValueType::Max);
                ranges.emplace_back(lowerBound, upperBound);
            }
        } else {
            LOG_DEBUG("Using join via IN clause");
            ranges.emplace_back(
                permanentBuffer->Capture(NTableClient::MinKey().Get()),
                permanentBuffer->Capture(NTableClient::MaxKey().Get()));

            auto inClause = New<TInOpExpression>(joinKeyExprs, MakeSharedRange(std::move(keys), permanentBuffer));

            subquery->WhereClause = subquery->WhereClause
                ? MakeAndExpression(inClause, subquery->WhereClause)
                : inClause;
        }

        LOG_DEBUG("Executing subquery");

        auto pipe = New<NTableClient::TSchemafulPipe>();

        TDataRanges dataSource;
        dataSource.Id = foreignDataId;
        dataSource.Ranges = MakeSharedRange(std::move(ranges), std::move(permanentBuffer));

        context->ExecuteCallback(subquery, dataSource, pipe->GetWriter());

        // Join rowsets.
        // allRows have format (join key... , other columns...)

        LOG_DEBUG("Joining started");

        std::vector<TRow> foreignRows;
        foreignRows.reserve(RowsetProcessingSize);

        auto reader = pipe->GetReader();

        std::vector<TRow> joinedRows;
        TRowBufferPtr intermediateBuffer = context->IntermediateBuffer;

        auto consumeJoinedRows = [&] () {
            // Consume joined rows.
            consumeRows(consumeRowsClosure, joinedRows.data(), joinedRows.size());
            joinedRows.clear();
            intermediateBuffer->Clear();
        };

        while (true) {
            bool hasMoreData = reader->Read(&foreignRows);
            bool shouldWait = foreignRows.empty();

            for (auto foreignRow : foreignRows) {
                auto it = joinLookup.find(foreignRow);

                if (it == joinLookup.end()) {
                    continue;
                }

                int startIndex = it->second.first;
                bool& isJoined = it->second.second;

                for (
                    int chainedRowIndex = startIndex;
                    chainedRowIndex >= 0;
                    chainedRowIndex = chainedRows[chainedRowIndex].second)
                {
                    auto row = chainedRows[chainedRowIndex].first;
                    auto joinedRow = TMutableRow::Allocate(intermediateBuffer->GetPool(), columnMapping.size());

                    for (size_t column = 0; column < columnMapping.size(); ++column) {
                        const auto& joinedColumn = columnMapping[column];

                        joinedRow[column] = joinedColumn.first
                            ? row[joinedColumn.second]
                            : foreignRow[joinedColumn.second];
                    }

                    joinedRows.push_back(joinedRow);

                    if (joinedRows.size() >= RowsetProcessingSize) {
                        consumeJoinedRows();
                    }
                }
                isJoined = true;
            }

            consumeJoinedRows();

            foreignRows.clear();

            if (!hasMoreData) {
                break;
            }

            if (shouldWait) {
                NProfiling::TAggregatingTimingGuard timingGuard(&context->Statistics->AsyncTime);
                WaitFor(reader->GetReadyEvent())
                    .ThrowOnError();
            }
        }

        if (isLeft) {
            for (auto lookup : joinLookup) {
                int startIndex = lookup.second.first;
                bool isJoined = lookup.second.second;

                if (isJoined) {
                    continue;
                }

                for (
                    int chainedRowIndex = startIndex;
                    chainedRowIndex >= 0;
                    chainedRowIndex = chainedRows[chainedRowIndex].second)
                {
                    auto row = chainedRows[chainedRowIndex].first;
                    auto joinedRow = TMutableRow::Allocate(intermediateBuffer->GetPool(), columnMapping.size());

                    for (size_t column = 0; column < columnMapping.size(); ++column) {
                        const auto& joinedColumn = columnMapping[column];

                        joinedRow[column] = joinedColumn.first
                            ? row[joinedColumn.second]
                            : MakeUnversionedSentinelValue(EValueType::Null);
                    }

                    joinedRows.push_back(joinedRow);
                }

                if (joinedRows.size() >= RowsetProcessingSize) {
                    consumeJoinedRows();
                }
            }
        }

        consumeJoinedRows();

        LOG_DEBUG("Joining finished");
    };
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT
