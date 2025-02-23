#pragma once

#include <Processors/IProcessor.h>
#include <Processors/RowsBeforeLimitCounter.h>
#include <Core/SortDescription.h>
#include <Common/serde.h>
namespace DB
{
namespace Streaming
{

/// Implementation for LIMIT N OFFSET M
/// This processor support multiple inputs and outputs (the same number).
/// Each pair of input and output port works independently.
/// The reason to have multiple ports is to be able to stop all sources when limit is reached, in a query like:
///     SELECT * FROM system.numbers_mt WHERE number = 1000000 LIMIT 1
///
/// always_read_till_end - read all data from input ports even if limit was reached.
/// with_ties, description - implementation of LIMIT WITH TIES. It works only for single port.
class LimitTransform final : public IProcessor
{
private:
    UInt64 limit;
    UInt64 offset;

    bool always_read_till_end;

    bool with_ties;
    const SortDescription description;

    SERDE Chunk previous_row_chunk;  /// for WITH TIES, contains only sort columns
    std::vector<size_t> sort_column_positions;

    SERDE UInt64 rows_read = 0; /// including the last read block
    SERDE RowsBeforeLimitCounterPtr rows_before_limit_at_least;

    /// State of port's pair.
    /// Chunks from different port pairs are not mixed for better cache locality.
    struct PortsData
    {
        Chunk current_chunk;

        InputPort * input_port = nullptr;
        OutputPort * output_port = nullptr;
        bool is_finished = false;
        /// For checkpoint
        bool request_checkpoint = false;
    };

    std::vector<PortsData> ports_data;
    size_t num_finished_port_pairs = 0;

    /// proton: starts. For checkpoint
    size_t curr_num_requested_checkpoint = 0;
    /// proton: ends.

    Chunk makeChunkWithPreviousRow(const Chunk & current_chunk, UInt64 row_num) const;
    ColumnRawPtrs extractSortColumns(const Columns & columns) const;
    bool sortColumnsEqualAt(const ColumnRawPtrs & current_chunk_sort_columns, UInt64 current_chunk_row_num) const;

public:
    LimitTransform(
        const Block & header_, UInt64 limit_, UInt64 offset_, size_t num_streams = 1,
        bool always_read_till_end_ = false, bool with_ties_ = false,
        SortDescription description_ = {});

    String getName() const override { return "StreamingLimit"; }

    Status prepare(const PortNumbers & /*updated_input_ports*/, const PortNumbers & /*updated_output_ports*/) override;
    Status prepare() override; /// Compatibility for TreeExecutor.
    Status preparePair(PortsData & data);
    void splitChunk(PortsData & data);

    /// proton: starts.
    void work() override;
    void checkpoint(CheckpointContextPtr ckpt_ctx) override;
    void recover(CheckpointContextPtr ckpt_ctx) override;
    /// proton: ends.

    InputPort & getInputPort() { return inputs.front(); }
    OutputPort & getOutputPort() { return outputs.front(); }

    void setRowsBeforeLimitCounter(RowsBeforeLimitCounterPtr counter) { rows_before_limit_at_least.swap(counter); }
};

}
}
