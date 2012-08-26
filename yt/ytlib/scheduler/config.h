#pragma once

#include "public.h"

#include <ytlib/ytree/yson_serializable.h>

#include <ytlib/table_client/config.h>

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////////////////

struct TOperationSpecBase
    : public TYsonSerializable
{
    TOperationSpecBase()
    {
        SetKeepOptions(true);
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TUserJobSpec
    : public TYsonSerializable
{
    Stroka Command;
    
    std::vector<NYTree::TYPath> FilePaths;

    NYTree::INodePtr Format;
    NYTree::INodePtr InputFormat;
    NYTree::INodePtr OutputFormat;
    
    int CoresLimit;
    i64 MemoryLimit;

    TUserJobSpec()
    {
        Register("command", Command);
        Register("file_paths", FilePaths)
            .Default();
        Register("format", Format)
            .Default(NULL);
        Register("input_format", InputFormat)
            .Default(NULL);
        Register("output_format", OutputFormat)
            .Default(NULL);
        Register("cores_limit", CoresLimit)
            .Default(1);
        Register("memory_limit", MemoryLimit)
            .Default((i64) 1024 * 1024 * 1024);
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TMapOperationSpec
    : public TOperationSpecBase
{
    TUserJobSpecPtr Mapper;   
    std::vector<NYTree::TYPath> InputTablePaths;
    std::vector<NYTree::TYPath> OutputTablePaths;
    TNullable<int> JobCount;
    i64 JobSliceDataSize;
    i64 MaxDataSizePerJob;
    TDuration LocalityTimeout;
    NYTree::INodePtr JobIO;

    TMapOperationSpec()
    {
        Register("mapper", Mapper);
        Register("input_table_paths", InputTablePaths);
        Register("output_table_paths", OutputTablePaths);
        Register("job_count", JobCount)
            .Default()
            .GreaterThan(0);
        Register("job_slice_data_size", JobSliceDataSize)
            .Default((i64) 256 * 1024 * 1024)
            .GreaterThan(0);
        Register("max_data_size_per_job", MaxDataSizePerJob)
            .Default((i64) 1024 * 1024 * 1024)
            .GreaterThan(0);
        Register("locality_timeout", LocalityTimeout)
            .Default(TDuration::Seconds(5));
        Register("job_io", JobIO)
            .Default(NULL);
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TMergeOperationSpecBase
    : public TOperationSpecBase
{
    //! During sorted merge the scheduler tries to ensure that large connected
    //! groups of chunks are partitioned into tasks of this or smaller size.
    //! This number, however, is merely an estimate, i.e. some tasks may still
    //! be larger.
    i64 MaxDataSizePerJob;

    TDuration LocalityTimeout;
    NYTree::INodePtr JobIO;

    TMergeOperationSpecBase()
    {
        Register("max_data_size_per_job", MaxDataSizePerJob)
            .Default((i64) 1024 * 1024 * 1024)
            .GreaterThan(0);
        Register("locality_timeout", LocalityTimeout)
            .Default(TDuration::Seconds(5));
        Register("job_io", JobIO)
            .Default(NULL);
    }
};

////////////////////////////////////////////////////////////////////////////////

DECLARE_ENUM(EMergeMode,
    (Sorted)
    (Ordered)
    (Unordered)
);

struct TMergeOperationSpec
    : public TMergeOperationSpecBase
{
    std::vector<NYTree::TYPath> InputTablePaths;
    NYTree::TYPath OutputTablePath;
    EMergeMode Mode;
    bool CombineChunks;
    TNullable< std::vector<Stroka> > MergeBy;

    TMergeOperationSpec()
    {
        Register("input_table_paths", InputTablePaths);
        Register("output_table_path", OutputTablePath);
        Register("mode", Mode)
            .Default(EMergeMode::Unordered);
        Register("combine_chunks", CombineChunks)
            .Default(false);
        Register("merge_by", MergeBy)
            .Default();
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TEraseOperationSpec
    : public TMergeOperationSpecBase
{
    NYTree::TYPath TablePath;
    bool CombineChunks;

    TEraseOperationSpec()
    {
        Register("table_path", TablePath);
        Register("combine_chunks", CombineChunks)
            .Default(false);
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TReduceOperationSpec
    : public TMergeOperationSpecBase
{
    TUserJobSpecPtr Reducer;
    std::vector<NYTree::TYPath> InputTablePaths;
    std::vector<NYTree::TYPath> OutputTablePaths;
    TNullable< std::vector<Stroka> > ReduceBy;

    TReduceOperationSpec()
    {
        Register("reducer", Reducer);
        Register("input_table_paths", InputTablePaths);
        Register("output_table_paths", OutputTablePaths);
        Register("reduce_by", ReduceBy)
            .Default();
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TSortOperationSpecBase
    : public TOperationSpecBase
{
    std::vector<NYTree::TYPath> InputTablePaths;

    TNullable<int> PartitionCount;

    TNullable<int> PartitionJobCount;
    i64 PartitionJobSliceDataSize;

    //! Only used if no partitioning is done.
    i64 SortJobSliceDataSize;

    //! Maximum amount of (uncompressed) data to be given to a single partition job.
    i64 MaxDataSizePerPartitionJob;

    //! Maximum amount of (uncompressed) data to be given to a single sort job.
    //! By default, the number of partitions is computed as follows:
    //! \code
    //! partitionCount = ceil(totalDataSize / MaxDataSizePerSortJob * PartitionCountBoostFactor)
    //! \endcode
    //! The user, however, may override this by specifying #PartitionCount explicitly.
    //! Here #PartitionCountBoostFactor accounts for uneven partition sizes and
    //! enables to fit most of sort jobs into #MaxWeightPerSortJob.
    i64 MaxDataSizePerSortJob;

    //! See comments for #MaxWeightPerSortJob.
    double PartitionCountBoostFactor;

    //! Maximum amount of (uncompressed) data to be given to a single unordered merge job
    //! that takes care of a megalomaniac partition.
    i64 MaxDataSizePerUnorderedMergeJob;

    double ShuffleStartThreshold;
    double MergeStartThreshold;

    TDuration PartitionLocalityTimeout;
    TDuration SortLocalityTimeout;
    TDuration MergeLocalityTimeout;

    int ShuffleNetworkLimit;

    TSortOperationSpecBase()
    {
        Register("input_table_paths", InputTablePaths);
        Register("partition_count", PartitionCount)
            .Default()
            .GreaterThan(0);
        Register("max_data_size_per_sort_job", MaxDataSizePerSortJob)
            .Default((i64) 1024 * 1024 * 1024)
            .GreaterThan(0);
        Register("partition_count_boost_factor", PartitionCountBoostFactor)
            .Default(1.5)
            .GreaterThanOrEqual(1.0);
        Register("shuffle_start_threshold", ShuffleStartThreshold)
            .Default(0.75)
            .InRange(0.0, 1.0);
        Register("merge_start_threshold", MergeStartThreshold)
            .Default(0.9)
            .InRange(0.0, 1.0);
        Register("shuffle_network_limit", ShuffleNetworkLimit)
            .Default(20);
    }

};

////////////////////////////////////////////////////////////////////////////////

struct TSortOperationSpec
    : public TSortOperationSpecBase
{
    NYTree::TYPath OutputTablePath;
    
    std::vector<Stroka> SortBy;

    // Desired number of samples per partition.
    int SamplesPerPartition;

    //! Only used if no partitioning is done.
    TNullable<int> SortJobCount;

    NYTree::INodePtr PartitionJobIO;
    NYTree::INodePtr SortJobIO;
    NYTree::INodePtr MergeJobIO;

    TSortOperationSpec()
    {
        Register("output_table_path", OutputTablePath);
        Register("sort_by", SortBy)
            .NonEmpty();
        Register("sort_job_count", SortJobCount)
            .Default()
            .GreaterThan(0);
        Register("samples_per_partition", SamplesPerPartition)
            .Default(10)
            .GreaterThan(1);
        Register("partition_job_io", PartitionJobIO)
            .Default(NULL);
        Register("sort_job_io", SortJobIO)
            .Default(NULL);
        Register("merge_job_io", MergeJobIO)
            .Default(NULL);

        // Provide custom names for shared settings.
        Register("partition_job_count", PartitionJobCount)
            .Default()
            .GreaterThan(0);
        Register("partition_job_slice_data_size", PartitionJobSliceDataSize)
            .Default((i64) 256 * 1024 * 1024)
            .GreaterThan(0);
        Register("sort_job_slice_data_size", SortJobSliceDataSize)
            .Default((i64) 256 * 1024 * 1024)
            .GreaterThan(0);
        Register("max_data_size_per_partition_job", MaxDataSizePerPartitionJob)
            .Default((i64) 1024 * 1024 * 1024)
            .GreaterThan(0);
        Register("max_data_size_per_unordered_merge_job", MaxDataSizePerUnorderedMergeJob)
            .Default((i64) 1024 * 1024 * 1024)
            .GreaterThan(0);
        Register("partition_locality_timeout", PartitionLocalityTimeout)
            .Default(TDuration::Seconds(5));
        Register("sort_locality_timeout", SortLocalityTimeout)
            .Default(TDuration::Seconds(10));
        Register("merge_locality_timeout", MergeLocalityTimeout)
            .Default(TDuration::Seconds(10));
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TMapReduceOperationSpec
    : public TSortOperationSpecBase
{
    std::vector<NYTree::TYPath> OutputTablePaths;

    std::vector<Stroka> SortBy;
    std::vector<Stroka> ReduceBy;

    TUserJobSpecPtr Mapper;
    TUserJobSpecPtr Reducer;

    NYTree::INodePtr MapJobIO;
    NYTree::INodePtr SortJobIO;
    NYTree::INodePtr ReduceJobIO;

    TMapReduceOperationSpec()
    {
        Register("output_table_paths", OutputTablePaths);
        Register("sort_by", SortBy)
            .NonEmpty();
        Register("reduce_by", ReduceBy)
            .Default(std::vector<Stroka>());
        Register("mapper", Mapper)
            .Default();
        Register("reducer", Reducer);
        Register("map_job_io", MapJobIO)
            .Default(NULL);
        Register("sort_job_io", SortJobIO)
            .Default(NULL);
        Register("reduce_job_io", ReduceJobIO)
            .Default(NULL);

        // Provide custom names for shared settings.
        Register("map_job_count", PartitionJobCount)
            .Default()
            .GreaterThan(0);
        Register("map_job_slice_data_size", PartitionJobSliceDataSize)
            .Default((i64) 256 * 1024 * 1024)
            .GreaterThan(0);
        Register("max_data_size_per_map_job", MaxDataSizePerPartitionJob)
            .Default((i64) 1024 * 1024 * 1024)
            .GreaterThan(0);
        Register("map_locality_timeout", PartitionLocalityTimeout)
            .Default(TDuration::Seconds(5));

        // The following settings are inherited from base but make no sense for map-reduce:
        //   SortJobSliceDataSize
        //   MaxDataSizePerUnorderedMergeJob
        //   SortLocalityTimeout
        //   MergeLocalityTimeout
    }

    virtual void OnLoaded() override
    {
        if (ReduceBy.empty()) {
            ReduceBy = SortBy;
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
