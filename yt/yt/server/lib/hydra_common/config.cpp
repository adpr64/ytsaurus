#include "config.h"

#include <yt/yt/client/api/config.h>

namespace NYT::NHydra {

////////////////////////////////////////////////////////////////////////////////

void TFileChangelogConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("data_flush_size", &TThis::DataFlushSize)
        .Alias("flush_buffer_size")
        .GreaterThanOrEqual(0)
        .Default(16_MB);
    registrar.Parameter("index_flush_size", &TThis::IndexFlushSize)
        .GreaterThanOrEqual(0)
        .Default(16_MB);
    registrar.Parameter("flush_period", &TThis::FlushPeriod)
        .Default(TDuration::MilliSeconds(10));
    registrar.Parameter("enable_sync", &TThis::EnableSync)
        .Default(true);
    registrar.Parameter("preallocate_size", &TThis::PreallocateSize)
        .GreaterThan(0)
        .Default();
    registrar.Parameter("recovery_buffer_size", &TThis::RecoveryBufferSize)
        .GreaterThan(0)
        .Default(16_MB);
}

////////////////////////////////////////////////////////////////////////////////

void TFileChangelogDispatcherConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("io_class", &TThis::IOClass)
        .Default(1); // IOPRIO_CLASS_RT
    registrar.Parameter("io_priority", &TThis::IOPriority)
        .Default(3);
    registrar.Parameter("flush_quantum", &TThis::FlushQuantum)
        .Default(TDuration::MilliSeconds(10));
}

////////////////////////////////////////////////////////////////////////////////

void TFileChangelogStoreConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("path", &TThis::Path);
    registrar.Parameter("changelog_reader_cache", &TThis::ChangelogReaderCache)
        .DefaultNew();

    registrar.Parameter("io_engine_type", &TThis::IOEngineType)
        .Default(NIO::EIOEngineType::ThreadPool);
    registrar.Parameter("io_engine", &TThis::IOConfig)
        .Optional();

    registrar.Preprocessor([] (TThis* config) {
        config->ChangelogReaderCache->Capacity = 4;
    });
}

////////////////////////////////////////////////////////////////////////////////

TLocalSnapshotStoreConfig::TLocalSnapshotStoreConfig()
{
    RegisterParameter("path", Path);
    RegisterParameter("codec", Codec)
        .Default(NCompression::ECodec::Lz4);
}

////////////////////////////////////////////////////////////////////////////////

TRemoteSnapshotStoreConfig::TRemoteSnapshotStoreConfig()
{
    RegisterParameter("reader", Reader)
        .DefaultNew();
    RegisterParameter("writer", Writer)
        .DefaultNew();

    RegisterPreprocessor([&] {
        Reader->WorkloadDescriptor.Category = EWorkloadCategory::SystemTabletRecovery;
        Writer->WorkloadDescriptor.Category = EWorkloadCategory::SystemTabletSnapshot;

        //! We want to evenly distribute snapshot load across the cluster.
        Writer->PreferLocalHost = false;
    });
}

////////////////////////////////////////////////////////////////////////////////

TRemoteChangelogStoreConfig::TRemoteChangelogStoreConfig()
{
    RegisterParameter("reader", Reader)
        .DefaultNew();
    RegisterParameter("writer", Writer)
        .DefaultNew();
    RegisterParameter("lock_transaction_timeout", LockTransactionTimeout)
        .Default();

    RegisterPreprocessor([&] {
        Reader->WorkloadDescriptor.Category = EWorkloadCategory::SystemTabletRecovery;

        Writer->WorkloadDescriptor.Category = EWorkloadCategory::SystemTabletLogging;
        Writer->MaxChunkRowCount = 1'000'000'000;
        Writer->MaxChunkDataSize = 1_TB;
        Writer->MaxChunkSessionDuration = TDuration::Hours(24);
    });
}

////////////////////////////////////////////////////////////////////////////////

THydraJanitorConfig::THydraJanitorConfig()
{
    RegisterParameter("max_snapshot_count_to_keep", MaxSnapshotCountToKeep)
        .GreaterThanOrEqual(0)
        .Default(10);
    RegisterParameter("max_snapshot_size_to_keep", MaxSnapshotSizeToKeep)
        .GreaterThanOrEqual(0)
        .Default();
    RegisterParameter("max_changelog_count_to_keep", MaxChangelogCountToKeep)
        .GreaterThanOrEqual(0)
        .Default();
    RegisterParameter("max_changelog_size_to_keep", MaxChangelogSizeToKeep)
        .GreaterThanOrEqual(0)
        .Default();
}

////////////////////////////////////////////////////////////////////////////////

TLocalHydraJanitorConfig::TLocalHydraJanitorConfig()
{
    RegisterParameter("cleanup_period", CleanupPeriod)
        .Default(TDuration::Seconds(10));
}

////////////////////////////////////////////////////////////////////////////////

TDistributedHydraManagerConfig::TDistributedHydraManagerConfig()
{
    RegisterParameter("control_rpc_timeout", ControlRpcTimeout)
        .Default(TDuration::Seconds(5));

    RegisterParameter("max_commit_batch_duration", MaxCommitBatchDuration)
        .Default(TDuration::MilliSeconds(100));
    RegisterParameter("leader_lease_check_period", LeaderLeaseCheckPeriod)
        .Default(TDuration::Seconds(2));
    RegisterParameter("leader_lease_timeout", LeaderLeaseTimeout)
        .Default(TDuration::Seconds(5));
    RegisterParameter("leader_lease_grace_delay", LeaderLeaseGraceDelay)
        .Default(TDuration::Seconds(6));
    RegisterParameter("disable_leader_lease_grace_delay", DisableLeaderLeaseGraceDelay)
        .Default(false);

    RegisterParameter("commit_flush_rpc_timeout", CommitFlushRpcTimeout)
        .Default(TDuration::Seconds(15));
    RegisterParameter("commit_forwarding_rpc_timeout", CommitForwardingRpcTimeout)
        .Default(TDuration::Seconds(30));

    RegisterParameter("restart_backoff_time", RestartBackoffTime)
        .Default(TDuration::Seconds(5));

    RegisterParameter("snapshot_build_timeout", SnapshotBuildTimeout)
        .Default(TDuration::Minutes(5));
    RegisterParameter("snapshot_fork_timeout", SnapshotForkTimeout)
        .Default(TDuration::Minutes(2));
    RegisterParameter("snapshot_build_period", SnapshotBuildPeriod)
        .Default(TDuration::Minutes(60));
    RegisterParameter("snapshot_build_splay", SnapshotBuildSplay)
        .Default(TDuration::Minutes(5));

    RegisterParameter("changelog_download_rpc_timeout", ChangelogDownloadRpcTimeout)
        .Default(TDuration::Seconds(10));
    RegisterParameter("max_changelog_records_per_request", MaxChangelogRecordsPerRequest)
        .GreaterThan(0)
        .Default(64 * 1024);
    RegisterParameter("max_changelog_bytes_per_request", MaxChangelogBytesPerRequest)
        .GreaterThan(0)
        .Default(128_MB);

    RegisterParameter("snapshot_download_rpc_timeout", SnapshotDownloadRpcTimeout)
        .Default(TDuration::Seconds(10));
    RegisterParameter("snapshot_download_block_size", SnapshotDownloadBlockSize)
        .GreaterThan(0)
        .Default(32_MB);

    RegisterParameter("snapshot_download_total_streaming_timeout", SnapshotDownloadTotalStreamingTimeout)
        .Default(TDuration::Minutes(30));
    RegisterParameter("snapshot_download_streaming_stall_timeout", SnapshotDownloadStreamingStallTimeout)
        .Default(TDuration::Seconds(30));
    RegisterParameter("snapshot_download_window", SnapshotDownloadWindowSize)
        .GreaterThan(0)
        .Default(32_MB);
    RegisterParameter("snapshot_download_streaming_compression_codec", SnapshotDownloadStreamingCompressionCodec)
        .Default(NCompression::ECodec::Lz4);

    RegisterParameter("max_commit_batch_delay", MaxCommitBatchDelay)
        .Default(TDuration::MilliSeconds(10));
    RegisterParameter("max_commit_batch_record_count", MaxCommitBatchRecordCount)
        .Default(10'000);

    RegisterParameter("mutation_serialization_period", MutationSerializationPeriod)
        .Default(TDuration::MilliSeconds(5));
    RegisterParameter("mutation_flush_period", MutationFlushPeriod)
        .Default(TDuration::MilliSeconds(5));

    RegisterParameter("leader_sync_delay", LeaderSyncDelay)
        .Default(TDuration::MilliSeconds(10));

    RegisterParameter("max_changelog_record_count", MaxChangelogRecordCount)
        .Default(1'000'000)
        .GreaterThan(0);
    RegisterParameter("max_changelog_data_size", MaxChangelogDataSize)
        .Default(1_GB)
        .GreaterThan(0);
    RegisterParameter("preallocate_changelogs", PreallocateChangelogs)
        .Default(false);
    RegisterParameter("close_changelogs", CloseChangelogs)
        .Default(true);

    RegisterParameter("heartbeat_mutation_period", HeartbeatMutationPeriod)
        .Default(TDuration::Seconds(60));
    RegisterParameter("heartbeat_mutation_timeout", HeartbeatMutationTimeout)
        .Default(TDuration::Seconds(60));

    RegisterParameter("changelog_record_count_check_retry_period", ChangelogRecordCountCheckRetryPeriod)
        .Default(TDuration::Seconds(1));

    RegisterParameter("mutation_logging_suspension_timeout", MutationLoggingSuspensionTimeout)
        .Default(TDuration::Seconds(60));

    RegisterParameter("build_snapshot_delay", BuildSnapshotDelay)
        .Default(TDuration::Zero());

    RegisterParameter("min_persistent_store_initialization_backoff_time", MinPersistentStoreInitializationBackoffTime)
        .Default(TDuration::MilliSeconds(200));
    RegisterParameter("max_persistent_store_initialization_backoff_time", MaxPersistentStoreInitializationBackoffTime)
        .Default(TDuration::Seconds(5));
    RegisterParameter("persistent_store_initialization_backoff_time_multiplier", PersistentStoreInitializationBackoffTimeMultiplier)
        .Default(1.5);

    RegisterParameter("abandon_leader_lease_request_timeout", AbandonLeaderLeaseRequestTimeout)
        .Default(TDuration::Seconds(5));

    RegisterParameter("force_mutation_logging", ForceMutationLogging)
        .Default(false);

    RegisterParameter("enable_state_hash_checker", EnableStateHashChecker)
        .Default(true);

    RegisterParameter("max_state_hash_checker_entry_count", MaxStateHashCheckerEntryCount)
        .GreaterThan(0)
        .Default(1000);

    RegisterParameter("state_hash_checker_mutation_verification_sampling_rate", StateHashCheckerMutationVerificationSamplingRate)
        .GreaterThan(0)
        .Default(10);

    RegisterParameter("max_queued_mutation_count", MaxQueuedMutationCount)
        .GreaterThan(0)
        .Default(100'000);

    RegisterParameter("max_queued_mutation_data_size", MaxQueuedMutationDataSize)
        .GreaterThan(0)
        .Default(2_GB);

    RegisterParameter("leader_switch_timeout", LeaderSwitchTimeout)
        .Default(TDuration::Seconds(30));

    RegisterParameter("invariants_check_probability", InvariantsCheckProbability)
        .Default();

    RegisterParameter("max_in_flight_accept_mutations_request_count", MaxInFlightAcceptMutationsRequestCount)
        .GreaterThan(0)
        .Default(10);

    RegisterParameter("max_in_flight_mutations_count", MaxInFlightMutationCount)
        .GreaterThan(0)
        .Default(100000);

    RegisterParameter("max_in_flight_mutation_data_size", MaxInFlightMutationDataSize)
        .GreaterThan(0)
        .Default(2_GB);

    RegisterParameter("max_changelogs_for_recovery", MaxChangelogsForRecovery)
        .GreaterThan(0)
        .Default(20);

    RegisterParameter("max_changelog_mutation_count_for_recovery", MaxChangelogMutationCountForRecovery)
        .GreaterThan(0)
        .Default(20'000'000);

    RegisterParameter("max_total_changelog_size_for_recovery", MaxTotalChangelogSizeForRecovery)
        .GreaterThan(0)
        .Default(20_GB);

    RegisterParameter("checkpoint_check_period", CheckpointCheckPeriod)
        .Default(TDuration::Seconds(15));

    RegisterParameter("max_changelogs_to_create_during_acquisition", MaxChangelogsToCreateDuringAcquisition)
        .Default(10);

    RegisterParameter("alert_on_snapshot_failure", AlertOnSnapshotFailure)
        .Default(true);

    RegisterPostprocessor([&] {
        if (!DisableLeaderLeaseGraceDelay && LeaderLeaseGraceDelay <= LeaderLeaseTimeout) {
            THROW_ERROR_EXCEPTION("\"leader_lease_grace_delay\" must be larger than \"leader_lease_timeout\"");
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

TSerializationDumperConfig::TSerializationDumperConfig()
{
    RegisterParameter("lower_limit", LowerLimit)
        .GreaterThanOrEqual(0)
        .Default(0);
    RegisterParameter("upper_limit", UpperLimit)
        .GreaterThanOrEqual(0)
        .Default(std::numeric_limits<i64>::max());

    RegisterPostprocessor([&] () {
        if (LowerLimit >= UpperLimit) {
            THROW_ERROR_EXCEPTION("\"upper_limit\" must be greater than \"lower_limit\"")
                << TErrorAttribute("lower_limit", LowerLimit)
                << TErrorAttribute("upper_limit", UpperLimit);
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHydra
