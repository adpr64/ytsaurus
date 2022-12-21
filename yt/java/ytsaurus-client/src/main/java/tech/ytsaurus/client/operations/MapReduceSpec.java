package tech.ytsaurus.client.operations;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.stream.Collectors;

import javax.annotation.Nullable;

import tech.ytsaurus.client.TransactionalClient;
import tech.ytsaurus.core.DataSize;
import tech.ytsaurus.core.tables.SortColumn;
import tech.ytsaurus.ysontree.YTreeBuilder;

import ru.yandex.lang.NonNullApi;
import ru.yandex.lang.NonNullFields;

/**
 * Immutable map-reduce spec.
 * @see <a href="https://yt.yandex-team.ru/docs/description/mr/mapreduce">
 * map_reduce documentation
 * </a>
 */
@NonNullApi
@NonNullFields
public class MapReduceSpec extends UserOperationSpecBase implements Spec {
    private final List<String> reduceBy;
    private final List<SortColumn> sortBy;

    @Nullable
    private final UserJobSpec mapperSpec;
    @Nullable
    private final UserJobSpec reduceCombinerSpec;
    private final UserJobSpec reducerSpec;

    @Nullable
    private final Integer mapJobCount;
    @Nullable
    private final Integer partitionCount;
    @Nullable
    private final Integer partitionJobCount;

    @Nullable
    private final DataSize dataSizePerSortJob;
    @Nullable
    private final Integer mapperOutputTableCount;

    @Nullable
    private final JobIo mapJobIo;
    @Nullable
    private final JobIo sortJobIo;
    @Nullable
    private final JobIo reduceJobIo;

    protected <T extends BuilderBase<T>> MapReduceSpec(BuilderBase<T> builder) {
        super(builder);

        reduceBy = builder.reduceBy;
        sortBy = builder.sortBy;
        mapperSpec = builder.mapperSpec;
        reduceCombinerSpec = builder.reduceCombinerSpec;

        if (builder.reducerSpec == null) {
            throw new RuntimeException("reducerSpec is not specified");
        }
        reducerSpec = builder.reducerSpec;

        mapJobCount = builder.mapJobCount;
        partitionCount = builder.partitionCount;
        partitionJobCount = builder.partitionJobCount;

        dataSizePerSortJob = builder.dataSizePerSortJob;
        mapperOutputTableCount = builder.mapperOutputTableCount;

        mapJobIo = builder.mapJobIo;
        sortJobIo = builder.sortJobIo;
        reduceJobIo = builder.reduceJobIo;
    }

    @Override
    public int hashCode() {
        return super.hashCode();
    }

    @Override
    public boolean equals(@Nullable Object obj) {
        if (obj == null) {
            return false;
        }
        if (getClass() != obj.getClass()) {
            return false;
        }

        MapReduceSpec spec = (MapReduceSpec) obj;
        return reduceBy.equals(spec.reduceBy)
                && sortBy.equals(spec.sortBy)
                && Optional.ofNullable(mapperSpec).equals(Optional.ofNullable(spec.mapperSpec))
                && Optional.ofNullable(reduceCombinerSpec).equals(Optional.ofNullable(spec.reduceCombinerSpec))
                && reducerSpec.equals(spec.reducerSpec)
                && Optional.ofNullable(mapJobCount).equals(Optional.ofNullable(spec.mapJobCount))
                && Optional.ofNullable(partitionCount).equals(Optional.ofNullable(spec.partitionCount))
                && Optional.ofNullable(partitionJobCount).equals(Optional.ofNullable(spec.partitionJobCount))
                && Optional.ofNullable(dataSizePerSortJob).equals(Optional.ofNullable(spec.dataSizePerSortJob))
                && Optional.ofNullable(mapperOutputTableCount).equals(Optional.ofNullable(spec.mapperOutputTableCount))
                && Optional.ofNullable(mapJobIo).equals(Optional.ofNullable(spec.mapJobIo))
                && Optional.ofNullable(sortJobIo).equals(Optional.ofNullable(spec.sortJobIo))
                && Optional.ofNullable(reduceJobIo).equals(Optional.ofNullable(spec.reduceJobIo));
    }

    /**
     * @see Builder#setMapJobCount(Integer)
     */
    public Optional<Integer> getMapJobCount() {
        return Optional.ofNullable(mapJobCount);
    }

    /**
     * @see Builder#setPartitionCount(Integer)
     */
    public Optional<Integer> getPartitionCount() {
        return Optional.ofNullable(partitionCount);
    }

    /**
     * @see Builder#setPartitionJobCount(Integer)
     */
    public Optional<Integer> getPartitionJobCount() {
        return Optional.ofNullable(partitionJobCount);
    }

    /**
     * @see Builder#setDataSizePerSortJob(DataSize)
     */
    public Optional<DataSize> getDataSizePerSortJob() {
        return Optional.ofNullable(dataSizePerSortJob);
    }

    /**
     * @see Builder#setMapperOutputTableCount(Integer)
     */
    public Optional<Integer> getMapperOutputTableCount() {
        return Optional.ofNullable(mapperOutputTableCount);
    }

    /**
     * @see Builder#setMapperSpec(MapperSpec)
     */
    public Optional<UserJobSpec> getMapperSpec() {
        return Optional.ofNullable(mapperSpec);
    }

    /**
     * @see Builder#setSortBy(List)
     */
    public List<String> getSortBy() {
        return sortBy.stream().map(SortColumn::getName).collect(Collectors.toList());
    }

    /**
     * @see Builder#setSortByColumns(List)
     */
    public List<SortColumn> getSortByColumns() {
        return sortBy;
    }

    /**
     * @see Builder#setReduceBy(List)
     */
    public List<String> getReduceBy() {
        return reduceBy;
    }

    /**
     * @see Builder#setReduceCombinerSpec(ReducerSpec)
     */
    public Optional<UserJobSpec> getReduceCombinerSpec() {
        return Optional.ofNullable(reduceCombinerSpec);
    }

    /**
     * @see Builder#setReducerSpec(UserJobSpec)
     */
    public UserJobSpec getReducerSpec() {
        return reducerSpec;
    }

    /**
     * @see Builder#setMapJobIo(JobIo)
     */
    public Optional<JobIo> getMapJobIo() {
        return Optional.ofNullable(mapJobIo);
    }

    /**
     * @see Builder#setSortJobIo(JobIo)
     */
    public Optional<JobIo> getSortJobIo() {
        return Optional.ofNullable(sortJobIo);
    }

    /**
     * @see Builder#setReduceJobIo(JobIo)
     */
    public Optional<JobIo> getReduceJobIo() {
        return Optional.ofNullable(reduceJobIo);
    }

    /**
     * Create output tables, upload necessary jars and files to YT, and create spec as yson.
     */
    @Override
    public YTreeBuilder prepare(YTreeBuilder builder, TransactionalClient yt, SpecPreparationContext context) {
        SpecUtils.createOutputTables(yt, getOutputTables(), getOutputTableAttributes());
        @Nullable final String title;
        List<Title> titles = new ArrayList<>();
        if (mapperSpec != null) {
            Optional<String> mapperTitle = SpecUtils.getMapperOrReducerTitle(mapperSpec);
            mapperTitle.ifPresent(s -> titles.add(new Title("mapper", s)));
        }
        Optional<String> reducerTitle = SpecUtils.getMapperOrReducerTitle(reducerSpec);
        reducerTitle.ifPresent(s -> titles.add(new Title("reducer", s)));
        if (reduceCombinerSpec != null) {
            Optional<String> reduceCombinerTitle = SpecUtils.getMapperOrReducerTitle(reduceCombinerSpec);
            reduceCombinerTitle.ifPresent(s -> titles.add(new Title("reduce-combiner", s)));
        }
        if (titles.isEmpty()) {
            title = null;
        } else if (titles.size() == 1) {
            title = titles.get(0).title;
        } else {
            title = titles.stream().map(Title::toString).collect(Collectors.joining(", "));
        }

        int mapperOutputCount = 1 + Optional.ofNullable(mapperOutputTableCount).orElse(0);
        int reduceCombinerOutputCount = 1;
        int reducerOutputCount = getOutputTables().size() - Optional.ofNullable(mapperOutputTableCount).orElse(0);

        return builder.beginMap()
                .when(title != null, b -> b.key("title").value(title))
                .when(mapJobCount != null, b -> b.key("map_job_count").value(mapJobCount))
                .when(partitionCount != null, b -> b.key("partition_count").value(partitionCount))
                .when(partitionJobCount != null, b -> b.key("partition_job_count").value(partitionJobCount))
                .when(dataSizePerSortJob != null, b -> b.key("data_size_per_sort_job")
                        .value(Objects.requireNonNull(dataSizePerSortJob).toBytes()))
                .when(mapperSpec != null, b -> b.key("mapper").apply(b2 ->
                        Objects.requireNonNull(mapperSpec).prepare(b2, yt, context, mapperOutputCount)))
                .key("sort_by").value(sortBy, (b, t) -> t.toTree(b))
                .key("reduce_by").value(reduceBy)
                .key("reducer").apply(b -> reducerSpec.prepare(b, yt, context, reducerOutputCount))
                .when(reduceCombinerSpec != null, b -> b.key("reduce_combiner")
                        .apply(b2 -> Objects.requireNonNull(reduceCombinerSpec)
                                .prepare(b2, yt, context, reduceCombinerOutputCount)))
                .key("started_by").apply(b -> SpecUtils.startedBy(b, context))
                .when(mapperOutputTableCount != null,
                        b -> b.key("mapper_output_table_count").value(mapperOutputTableCount))
                .when(mapJobIo != null, b -> b.key("map_job_io")
                        .value(Objects.requireNonNull(mapJobIo).prepare()))
                .when(sortJobIo != null, b -> b.key("sort_job_io")
                        .value(Objects.requireNonNull(sortJobIo).prepare()))
                .when(reduceJobIo != null, b -> b.key("reduce_job_io")
                        .value(Objects.requireNonNull(reduceJobIo).prepare()))
                .apply(b -> toTree(b, context))
                .endMap();
    }

    /**
     * Create empty builder.
     */
    public static BuilderBase<?> builder() {
        return new Builder();
    }

    /**
     * Builder of {@link MapReduceSpec}.
     */
    @NonNullApi
    @NonNullFields
    public static class Builder extends BuilderBase<Builder> {
        @Override
        protected Builder self() {
            return this;
        }
    }

    // BuilderBase was taken out because there is another client
    // which we need to support too and which use the same MapReduceSpec class.
    @NonNullApi
    @NonNullFields
    public abstract static class BuilderBase<T extends BuilderBase<T>> extends UserOperationSpecBase.Builder<T> {
        private List<String> reduceBy = new ArrayList<>();
        private List<SortColumn> sortBy = new ArrayList<>();

        private @Nullable
        UserJobSpec mapperSpec;
        private @Nullable
        UserJobSpec reduceCombinerSpec;
        private @Nullable
        UserJobSpec reducerSpec;

        private @Nullable
        Integer mapJobCount;
        private @Nullable
        Integer partitionCount;
        private @Nullable
        Integer partitionJobCount;

        private @Nullable
        DataSize dataSizePerSortJob;
        private @Nullable
        Integer mapperOutputTableCount;

        private @Nullable
        JobIo mapJobIo;
        private @Nullable
        JobIo sortJobIo;
        private @Nullable
        JobIo reduceJobIo;

        /**
         * Create instance of {@link MapReduceSpec}.
         */
        public MapReduceSpec build() {
            return new MapReduceSpec(this);
        }

        /**
         * Set a list of columns by which reduce is carried out;
         */
        public T setReduceBy(List<String> reduceBy) {
            this.reduceBy = new ArrayList<>(reduceBy);
            return self();
        }

        /**
         * @see Builder#setReduceBy(List)
         */
        public T setReduceBy(String... reduceBy) {
            return setReduceBy(Arrays.asList(reduceBy));
        }

        /**
         * Set a list of columns by which the input tables are to be sorted.
         * The option enables an additional check for sorting of input tables
         * and guarantees that rows are sorted by a given set of columns inside a user script.
         * The reduceBy sequence of columns must be a prefix of the sortBy sequence of columns.
         */
        public T setSortByColumns(List<SortColumn> sortBy) {
            this.sortBy = new ArrayList<>(sortBy);
            return self();
        }

        /**
         * @see Builder#setSortByColumns(List)
         */
        public T setSortByColumns(SortColumn... sortBy) {
            return setSortByColumns(Arrays.asList(sortBy));
        }

        /**
         * @see Builder#setSortByColumns(List)
         */
        public T setSortBy(List<String> sortBy) {
            return setSortByColumns(SortColumn.convert(sortBy));
        }

        /**
         * @see Builder#setSortByColumns(List)
         */
        public T setSortBy(String... sortBy) {
            return setSortBy(Arrays.asList(sortBy));
        }

        /**
         * Set mapper spec.
         */
        public T setMapperSpec(@Nullable UserJobSpec mapperSpec) {
            this.mapperSpec = mapperSpec;
            return self();
        }

        /**
         * Set mapper spec.
         */
        public T setMapperSpec(@Nullable MapperSpec mapperSpec) {
            this.mapperSpec = mapperSpec;
            return self();
        }

        /**
         * Set reduce combiner spec.
         */
        public T setReduceCombinerSpec(@Nullable UserJobSpec reduceCombinerSpec) {
            this.reduceCombinerSpec = reduceCombinerSpec;
            return self();
        }

        /**
         * Set reduce combiner spec.
         */
        public T setReduceCombinerSpec(@Nullable ReducerSpec reduceCombinerSpec) {
            this.reduceCombinerSpec = reduceCombinerSpec;
            return self();
        }

        /**
         * Set reducer spec.
         */
        public T setReducerSpec(UserJobSpec reducerSpec) {
            this.reducerSpec = reducerSpec;
            return self();
        }

        /**
         * Set reducer spec.
         */
        public T setReducerSpec(ReducerSpec reducerSpec) {
            this.reducerSpec = reducerSpec;
            return self();
        }

        /**
         * Set how many jobs should be run in the map stage. It is advisory.
         */
        public T setMapJobCount(@Nullable Integer mapJobCount) {
            this.mapJobCount = mapJobCount;
            return self();
        }

        /**
         * Set how many partitions should be made in the sort. It is advisory.
         */
        public T setPartitionCount(@Nullable Integer partitionCount) {
            this.partitionCount = partitionCount;
            return self();
        }

        /**
         * Set how many partition jobs should be run. It is advisory.
         */
        public T setPartitionJobCount(@Nullable Integer partitionJobCount) {
            this.partitionJobCount = partitionJobCount;
            return self();
        }

        /**
         * Set recommended amount of input data for one sort job.
         */
        public T setDataSizePerSortJob(@Nullable DataSize dataSizePerSortJob) {
            this.dataSizePerSortJob = dataSizePerSortJob;
            return self();
        }

        /**
         * Set the number of tables from outputTablePaths that will be output from the map stage.
         * For such tables, table_index in the job is counted from one, and the zero output table is an intermediate output.
         */
        public T setMapperOutputTableCount(@Nullable Integer mapperOutputTableCount) {
            this.mapperOutputTableCount = mapperOutputTableCount;
            return self();
        }

        /**
         * Set job I/O options for map.
         */
        public T setMapJobIo(@Nullable JobIo mapJobIo) {
            this.mapJobIo = mapJobIo;
            return self();
        }

        /**
         * Set job I/O options for sort.
         */
        public T setSortJobIo(@Nullable JobIo sortJobIo) {
            this.sortJobIo = sortJobIo;
            return self();
        }

        /**
         * Set job I/O options for reduce.
         */
        public T setReduceJobIo(@Nullable JobIo reduceJobIo) {
            this.reduceJobIo = reduceJobIo;
            return self();
        }
    }

    private static class Title {
        final String name;
        final String title;

        Title(String name, String title) {
            this.name = name;
            this.title = title;
        }

        public String toString() {
            return this.name + ": " + this.title;
        }
    }
}
