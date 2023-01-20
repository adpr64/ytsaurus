#include "parameterized_balancing_helpers.h"

#include "balancing_helpers.h"
#include "config.h"
#include "table.h"
#include "tablet.h"
#include "tablet_cell.h"
#include "tablet_cell_bundle.h"

#include <yt/yt/client/object_client/helpers.h>

#include <yt/yt/client/table_client/unversioned_value.h>

#include <yt/yt/core/misc/collection_helpers.h>

#include <yt/yt/orm/library/query/expression_evaluator.h>

namespace NYT::NTabletBalancer {

using namespace NCypressClient;
using namespace NLogging;
using namespace NObjectClient;
using namespace NQueryClient;
using namespace NTableClient;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

static const std::vector<TString> ParameterizedBalancingAttributes = {
    "/statistics",
    "/performance_counters"
};

constexpr int MaxVerboseLogMessagesPerIteration = 1000;

////////////////////////////////////////////////////////////////////////////////

namespace {

double Sqr(double x)
{
    return x * x;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

class TParameterizedReassignSolver
    : public IParameterizedReassignSolver
{
public:
    TParameterizedReassignSolver(
        TTabletCellBundlePtr bundle,
        std::vector<TString> performanceCountersKeys,
        bool ignoreTableWiseConfig,
        int maxMoveActionCount,
        double deviationThreshold,
        const TLogger& logger);

    std::vector<TMoveDescriptor> BuildActionDescriptors() override;

private:
    using TApplyActionCallback = std::function<void(int*)>;

    struct TBestAction
    {
        double Metric;
        TApplyActionCallback Callback;
    };

    const TTabletCellBundlePtr Bundle_;
    const TLogger Logger;
    const int MaxMoveActionCount_;
    const bool IgnoreTableWiseConfig_;
    const std::vector<TString> PerformanceCountersKeys_;
    const double DeviationThreshold_;

    std::vector<TTabletCellPtr> Cells_;

    THashMap<const TTablet*, TTabletCellId> TabletToCell_;
    THashMap<TTabletCellId, double> CellToMetric_;
    THashMap<const TTablet*, double> TabletToMetric_;
    THashMap<TString, double> NodeToMetric_;

    double CurrentMetric_;
    TBestAction BestAction_;
    int LogMessageCount_ = 0;

    NOrm::NQuery::IExpressionEvaluatorPtr Evaluator_;

    void Initialize();

    double CalculateTotalBundleMetric() const;
    double GetTabletMetric(const TTabletPtr& tablet) const;

    void TryMoveTablet(
        const TTablet* tablet,
        const TTabletCell* cell);

    void TrySwapTablets(
        const TTablet* lhsTablet,
        const TTablet* rhsTablet);

    bool TryFindBestAction(bool canMakeSwap);
    bool ShouldTrigger() const;
};

TParameterizedReassignSolver::TParameterizedReassignSolver(
    TTabletCellBundlePtr bundle,
    std::vector<TString> performanceCountersKeys,
    bool ignoreTableWiseConfig,
    int maxMoveActionCount,
    double deviationThreshold,
    const TLogger& logger)
    : Bundle_(std::move(bundle))
    , Logger(logger.WithTag("BundleName: %v", Bundle_->Name))
    , MaxMoveActionCount_(maxMoveActionCount)
    , IgnoreTableWiseConfig_(ignoreTableWiseConfig)
    , PerformanceCountersKeys_(std::move(performanceCountersKeys))
    , DeviationThreshold_(deviationThreshold)
{ }

void TParameterizedReassignSolver::Initialize()
{
    THROW_ERROR_EXCEPTION_IF(!Bundle_->AreAllCellsAssignedToPeers(),
        "Not all cells are assigned to nodes");

    Cells_ = Bundle_->GetAliveCells();

    Evaluator_ = NOrm::NQuery::CreateExpressionEvaluator(
        Bundle_->Config->ParameterizedBalancingMetric,
        ParameterizedBalancingAttributes);

    for (const auto& cell : Cells_) {
        double cellMetric = 0;

        for (const auto& tablet : cell->Tablets) {
            if (!IgnoreTableWiseConfig_ && !tablet->Table->EnableParameterizedBalancing) {
                continue;
            }

            if (TypeFromId(tablet->Table->Id) != EObjectType::Table) {
                continue;
            }

            if (TypeFromId(tablet->Id) != EObjectType::Tablet) {
                continue;
            }

            auto tabletMetric = GetTabletMetric(tablet);

            if (tabletMetric < 0.0) {
                THROW_ERROR_EXCEPTION("Tablet metric must be nonnegative, got %v", tabletMetric)
                    << TErrorAttribute("tablet_metric_value", tabletMetric)
                    << TErrorAttribute("tablet_id", tablet->Id)
                    << TErrorAttribute("metric_formula", Bundle_->Config->ParameterizedBalancingMetric);
            } else if (tabletMetric == 0.0) {
                continue;
            }

            EmplaceOrCrash(TabletToMetric_, tablet.Get(), tabletMetric);
            EmplaceOrCrash(TabletToCell_, tablet.Get(), cell->Id);
            cellMetric += tabletMetric;
        }

        NodeToMetric_[*cell->NodeAddress] += cellMetric;

        EmplaceOrCrash(CellToMetric_, cell->Id, cellMetric);
        YT_LOG_DEBUG_IF(
            Bundle_->Config->EnableVerboseLogging,
            "Calculated cell metric (CellId: %v, CellMetric: %v)",
            cell->Id,
            cellMetric);
    }

    CurrentMetric_ = CalculateTotalBundleMetric();
    YT_VERIFY(CurrentMetric_ >= 0.);
}

bool TParameterizedReassignSolver::ShouldTrigger() const
{
    if (CellToMetric_.empty()) {
        return false;
    }

    auto [minCell, maxCell] = std::minmax_element(
        CellToMetric_.begin(),
        CellToMetric_.end(),
        [] (auto lhs, auto rhs) {
            return lhs.second < rhs.second;
        });

    YT_LOG_DEBUG_IF(
        Bundle_->Config->EnableVerboseLogging,
        "Arguments for checking whether parameterized balancing should trigger have been calculated "
        "(MinCellMetric: %v, MaxCellMetric: %v, DeviationThreshold: %v)",
        minCell->second,
        maxCell->second,
        DeviationThreshold_);

    return maxCell->second > minCell->second * (1 + DeviationThreshold_);
}

double TParameterizedReassignSolver::GetTabletMetric(const TTabletPtr& tablet) const
{
    auto value = Evaluator_->Evaluate({
        ConvertToYsonString(tablet->Statistics.OriginalNode),
        BuildTabletPerformanceCountersYson(tablet->PerformanceCountersProto, PerformanceCountersKeys_)
    }).ValueOrThrow();

    switch (value.Type) {
        case EValueType::Double:
            return value.Data.Double;

        case EValueType::Int64:
            return value.Data.Int64;

        case EValueType::Uint64:
            return value.Data.Uint64;

        default:
            THROW_ERROR_EXCEPTION(
                "Tablet metric value type is not numerical: got %v",
                value.Type);
    }
}

double TParameterizedReassignSolver::CalculateTotalBundleMetric() const
{
    double cellMetric = std::accumulate(
        CellToMetric_.begin(),
        CellToMetric_.end(),
        0.0,
        [] (double x, auto item) {
            return x + Sqr(item.second);
        });

    double nodeMetric = std::accumulate(
        NodeToMetric_.begin(),
        NodeToMetric_.end(),
        0.0,
        [] (double x, auto item) {
            return x + Sqr(item.second);
        });

    return cellMetric + nodeMetric;
};

void TParameterizedReassignSolver::TryMoveTablet(
    const TTablet* tablet,
    const TTabletCell* cell)
{
    auto newMetric = CurrentMetric_;
    double tabletMetric = GetOrCrash(TabletToMetric_, tablet);
    auto sourceCellId = GetOrCrash(TabletToCell_, tablet);
    auto sourceCell = GetOrCrash(CellToMetric_, sourceCellId);
    auto destinationCell = GetOrCrash(CellToMetric_, cell->Id);

    if (sourceCellId == cell->Id) {
        // Trying to move the tablet from the cell to itself.
        return;
    }

    auto sourceNode = *GetOrCrash(Bundle_->TabletCells, sourceCellId)->NodeAddress;
    if (sourceNode != cell->NodeAddress) {
        auto sourceNodeMetric = GetOrCrash(NodeToMetric_, sourceNode);
        auto destinationNodeMetric = GetOrCrash(NodeToMetric_, *cell->NodeAddress);

        newMetric -= Sqr(sourceNodeMetric) - Sqr(sourceNodeMetric - tabletMetric);
        newMetric += Sqr(destinationNodeMetric + tabletMetric) - Sqr(destinationNodeMetric);
    }

    newMetric -= Sqr(sourceCell) - Sqr(sourceCell - tabletMetric);
    newMetric += Sqr(destinationCell + tabletMetric) - Sqr(destinationCell);

    YT_LOG_DEBUG_IF(
        Bundle_->Config->EnableVerboseLogging && LogMessageCount_++ < MaxVerboseLogMessagesPerIteration,
        "Trying to move tablet to another cell (TabletId: %v, CellId: %v, CurrentMetric: %v, CurrentBestMetric: %v, "
        "NewMetric: %v, TabletMetric: %v, SourceCellMetric: %v, DestinationCellMetric: %v)",
        tablet->Id,
        cell->Id,
        CurrentMetric_,
        BestAction_.Metric,
        newMetric,
        tabletMetric,
        sourceCell,
        destinationCell);

    if (newMetric < BestAction_.Metric) {
        BestAction_.Metric = newMetric;

        BestAction_.Callback = [=, this] (int* availiableActionCount) {
            TabletToCell_[tablet] = cell->Id;
            CellToMetric_[sourceCellId] -= tabletMetric;
            CellToMetric_[cell->Id] += tabletMetric;
            *availiableActionCount -= 1;

            if (sourceNode != cell->NodeAddress) {
                NodeToMetric_[sourceNode] -= tabletMetric;
                NodeToMetric_[*cell->NodeAddress] += tabletMetric;
            } else {
                YT_LOG_WARNING("The best action is between cells on the same node "
                    "(NodeAddress: %v, TabletId: %v)",
                    sourceNode,
                    tablet->Id);
            }

            YT_LOG_DEBUG("Applying best action: moving tablet to another cell "
                "(TabletId: %v, SourceCellId: %v, DestinationCellId: %v, "
                "SourceNode: %v, DestinationNode: %v)",
                tablet->Id,
                sourceCellId,
                cell->Id,
                sourceNode,
                *cell->NodeAddress);
        };
    }
};

void TParameterizedReassignSolver::TrySwapTablets(
    const TTablet* lhsTablet,
    const TTablet* rhsTablet)
{
    if (lhsTablet->Id == rhsTablet->Id) {
        // It makes no sense to swap tablet with itself.
        return;
    }

    auto lhsCellId = GetOrCrash(TabletToCell_, lhsTablet);
    auto rhsCellId = GetOrCrash(TabletToCell_, rhsTablet);

    if (lhsCellId == rhsCellId) {
        // It makes no sense to swap tablets that are already on the same cell.
        return;
    }

    auto lhsCellMetric = GetOrCrash(CellToMetric_, lhsCellId);
    auto rhsCellMetric = GetOrCrash(CellToMetric_, rhsCellId);

    auto lhsTabletMetric = GetOrCrash(TabletToMetric_, lhsTablet);
    auto rhsTabletMetric = GetOrCrash(TabletToMetric_, rhsTablet);

    auto newMetric = CurrentMetric_;
    newMetric -= Sqr(lhsCellMetric) + Sqr(rhsCellMetric);
    newMetric += Sqr(lhsCellMetric - lhsTabletMetric + rhsTabletMetric);
    newMetric += Sqr(rhsCellMetric + lhsTabletMetric - rhsTabletMetric);

    auto lhsNode = *GetOrCrash(Bundle_->TabletCells, lhsCellId)->NodeAddress;
    auto rhsNode = *GetOrCrash(Bundle_->TabletCells, rhsCellId)->NodeAddress;

    if (lhsNode != rhsNode) {
        auto lhsNodeMetric = GetOrCrash(NodeToMetric_, lhsNode);
        auto rhsNodeMetric = GetOrCrash(NodeToMetric_, rhsNode);

        newMetric -= Sqr(lhsNodeMetric) + Sqr(rhsNodeMetric);
        newMetric += Sqr(lhsNodeMetric - lhsTabletMetric + rhsTabletMetric);
        newMetric += Sqr(rhsNodeMetric + lhsTabletMetric - rhsTabletMetric);
    }

    YT_LOG_DEBUG_IF(
        Bundle_->Config->EnableVerboseLogging && LogMessageCount_++ < MaxVerboseLogMessagesPerIteration,
        "Trying to swap tablets (LhsTabletId: %v, RhsTabletId: %v, LhsCellId: %v, RhsCellId: %v, "
        "CurrentMetric: %v, CurrentBestMetric: %v, NewMetric: %v, LhsTabletMetric: %v, "
        "RhsTabletMetric: %v, LhsCellMetric: %v, RhsCellMetric: %v)",
        lhsTablet->Id,
        rhsTablet->Id,
        lhsCellId,
        rhsCellId,
        CurrentMetric_,
        BestAction_.Metric,
        newMetric,
        lhsTabletMetric,
        rhsTabletMetric,
        lhsCellMetric,
        rhsCellMetric);

    if (newMetric < BestAction_.Metric) {
        BestAction_.Metric = newMetric;

        BestAction_.Callback = [=, this] (int* availiableActionCount) {
            TabletToCell_[lhsTablet] = rhsCellId;
            TabletToCell_[rhsTablet] = lhsCellId;
            CellToMetric_[lhsCellId] -= lhsTabletMetric;
            CellToMetric_[lhsCellId] += rhsTabletMetric;
            CellToMetric_[rhsCellId] += lhsTabletMetric;
            CellToMetric_[rhsCellId] -= rhsTabletMetric;
            *availiableActionCount -= 2;

            if (lhsNode != rhsNode) {
                NodeToMetric_[lhsNode] -= lhsTabletMetric;
                NodeToMetric_[lhsNode] += rhsTabletMetric;
                NodeToMetric_[rhsNode] += lhsTabletMetric;
                NodeToMetric_[rhsNode] -= rhsTabletMetric;
            } else {
                YT_LOG_WARNING("The best action is between cells on the same node "
                    "(NodeAddress: %v, LhsTabletId: %v, RhsTabletId: %v)",
                    lhsNode,
                    lhsTablet->Id,
                    rhsTablet->Id);
            }

            YT_LOG_DEBUG("Applying best action: swapping tablets "
                "(LhsTabletId: %v, RhsTabletId: %v, LhsCellId: %v, RhsCellId: %v "
                "LhsNode: %v, RhsNode: %v)",
                lhsTablet->Id,
                rhsTablet->Id,
                lhsCellId,
                rhsCellId,
                lhsNode,
                rhsNode);
        };
    }
};

bool TParameterizedReassignSolver::TryFindBestAction(bool canMakeSwap)
{
    BestAction_ = TBestAction{.Metric = CurrentMetric_};

    for (const auto& [tablet, _] : TabletToMetric_) {
        for (const auto& cell : Cells_) {
            TryMoveTablet(tablet, cell.Get());
        }

        if (!canMakeSwap) {
            // Swap two tablets takes two actions.
            YT_LOG_DEBUG_IF(
                Bundle_->Config->EnableVerboseLogging,
                "Swap cannot be done because there are not enough actions available");
            continue;
        }

        for (const auto& [anotherTablet, _] : TabletToMetric_) {
            TrySwapTablets(tablet, anotherTablet);
        }
    }

    return BestAction_.Metric < CurrentMetric_;
}

std::vector<TMoveDescriptor> TParameterizedReassignSolver::BuildActionDescriptors()
{
    Initialize();

    if (!ShouldTrigger()) {
        YT_LOG_DEBUG("Parameterized balancing was not triggered (DeviationThreshold: %v)",
            DeviationThreshold_);
        return {};
    }

    int availiableActionCount = MaxMoveActionCount_;
    while (availiableActionCount > 0) {
        LogMessageCount_ = 0;
        if (TryFindBestAction(/*canMakeSwap*/ availiableActionCount >= 2)) {
            YT_VERIFY(BestAction_.Callback);
            BestAction_.Callback(&availiableActionCount);

            YT_LOG_DEBUG_IF(
                Bundle_->Config->EnableVerboseLogging,
                "Total parameterized metric changed (Old: %v, New: %v)",
                CurrentMetric_,
                BestAction_.Metric);
            CurrentMetric_ = BestAction_.Metric;
        } else {
            YT_LOG_DEBUG("Metric-improving action was not found");
            break;
        }
    }

    std::vector<TMoveDescriptor> descriptors;
    for (const auto& [tablet, cellId] : TabletToCell_) {
        if (!tablet->Cell || tablet->Cell->Id != cellId) {
            descriptors.emplace_back(TMoveDescriptor{
                .TabletId = tablet->Id,
                .TabletCellId = cellId
            });
        }
    }

    if (std::ssize(descriptors) > MaxMoveActionCount_) {
        YT_LOG_ALERT(
            "Too many actions created during parametrized balancing (DescriptorCount: %v, MoveActionLimit: %v)",
            std::ssize(descriptors),
            MaxMoveActionCount_);
        return {};
    }

    YT_LOG_DEBUG(
        "Scheduled move actions for parameterized tablets balancing (ActionCount: %v, MoveActionLimit: %v)",
        std::ssize(descriptors),
        MaxMoveActionCount_);

    return descriptors;
}

////////////////////////////////////////////////////////////////////////////////

IParameterizedReassignSolverPtr CreateParameterizedReassignSolver(
    TTabletCellBundlePtr bundle,
    std::vector<TString> performanceCountersKeys,
    bool ignoreTableWiseConfig,
    int moveActionLimit,
    double deviationThreshold,
    const NLogging::TLogger& logger)
{
    return New<TParameterizedReassignSolver>(
        std::move(bundle),
        std::move(performanceCountersKeys),
        ignoreTableWiseConfig,
        moveActionLimit,
        deviationThreshold,
        logger);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletBalancer
