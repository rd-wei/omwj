#include "distribute_expand.h"
#include <iostream>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>
#include <unordered_map>
#include "debug_util.h"
#include "../utils/counted_ecalls.h"  // Includes both Enclave_u.h and ecall_wrapper.h
#include "distribute_manager.h"
#include "../batch/performance_timer.h"
#include "../../common/batch_types.h"

// Per-step timing accumulators (nanoseconds), reset each Execute() call
static uint64_t g_de_cumsum_ns   = 0;
static uint64_t g_de_sort_ns     = 0;
static uint64_t g_de_padding_ns  = 0;
static uint64_t g_de_distribute_ns = 0;
static uint64_t g_de_expand_ns   = 0;

// Debug functions are declared in debug_util.h
// debug_dump_table is already declared in debug_util.h

void DistributeExpand::Execute(JoinTreeNodePtr root, sgx_enclave_id_t eid) {
    // Reset per-step accumulators
    g_de_cumsum_ns    = 0;
    g_de_sort_ns      = 0;
    g_de_padding_ns   = 0;
    g_de_distribute_ns = 0;
    g_de_expand_ns    = 0;

    // Get all nodes to expand
    auto nodes = GetAllNodes(root);
    
    // Debug: Check tables right after getting nodes
    DEBUG_INFO("Distribute-Expand: Checking tables after GetAllNodes");
    for (auto& node : nodes) {
        if (node->get_table().size() > 0) {
            DEBUG_INFO("  Table %s[0]: field_type=%d, equality_type=%d",
                       node->get_table_name().c_str(),
                       node->get_table()[0].field_type, node->get_table()[0].equality_type);
        }
    }
    
    // Expand each table according to its final multiplicities
    for (auto& node : nodes) {
        // Expanding table
        
        // Debug: Check table before expansion
        DEBUG_INFO("Before ExpandSingleTable for %s", node->get_table_name().c_str());
        if (node->get_table().size() > 0) {
            DEBUG_INFO("  field_type=%d, equality_type=%d",
                       node->get_table()[0].field_type, node->get_table()[0].equality_type);
        }
        
        Table expanded = ExpandSingleTable(node->get_table(), eid);
        node->set_table(expanded);
    }

    printf("DE_TIMING: cumsum=%.6f sort=%.6f padding=%.6f distribute=%.6f expand=%.6f\n",
           (double)g_de_cumsum_ns    / 1e9,
           (double)g_de_sort_ns      / 1e9,
           (double)g_de_padding_ns   / 1e9,
           (double)g_de_distribute_ns / 1e9,
           (double)g_de_expand_ns    / 1e9);
    printf("AKS_OCALL_TIMING: scan_io=%.6f swap_io=%.6f\n",
           (double)g_aks_scan_ocall_ns.load(std::memory_order_relaxed) / 1e9,
           (double)g_aks_swap_ocall_ns.load(std::memory_order_relaxed) / 1e9);
}

Table DistributeExpand::ExpandSingleTable(const Table& table, sgx_enclave_id_t eid) {
    if (table.size() == 0) {
        DEBUG_INFO("Empty table, nothing to expand");
        return table;  // Empty table, nothing to expand
    }
    
    DEBUG_INFO("Expanding table with %zu entries", table.size());
    
    // Get table name for debug output
    std::string table_name = table.get_table_name();
    DEBUG_INFO("Table name: %s", table_name.c_str());
    
    // Targeted debug: Check final_mult values before expansion
    uint32_t key_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_LOCAL_MULT | 
                       DEBUG_COL_FINAL_MULT | DEBUG_COL_FIELD_TYPE;
    debug_dump_with_mask(table, ("pre_expand_" + table_name).c_str(), 
                        ("distexp_pre_expand_" + table_name).c_str(), static_cast<uint32_t>(eid), key_mask);
    
    // Step 1: Initialize dst_idx field to 0
    DEBUG_INFO("Step 1 - Initializing dst_idx");
    Table working = table.batched_map(eid, OP_ECALL_TRANSFORM_INIT_DST_IDX);
    DEBUG_INFO("Step 1 complete");

    // Step 2: Compute cumulative sum of final_mult to get dst_idx
    DEBUG_INFO("Step 2 - Computing cumulative sum");
    { uint64_t t0 = get_current_time_ns();
      working.batched_linear_pass(eid, OP_ECALL_WINDOW_COMPUTE_DST_IDX);
      g_de_cumsum_ns += get_current_time_ns() - t0; }
    DEBUG_INFO("Step 2 complete");
    
    // Debug: Show dst_idx values after cumulative sum
    uint32_t dst_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_FINAL_MULT | DEBUG_COL_DST_IDX;
    debug_dump_with_mask(working, ("step2_dst_idx_" + table_name).c_str(),
                        ("distexp_step2_cumsum_" + table_name).c_str(), static_cast<uint32_t>(eid), dst_mask);
    
    // Step 3: Get output size from last entry
    DEBUG_INFO("Step 3 - Getting output size");
    size_t output_size = ComputeOutputSize(working, eid);
    DEBUG_INFO("Output size will be %zu", output_size);
    
    if (output_size == 0) {
        // All entries have final_mult = 0
        Table empty(table.get_table_name(), table.get_schema());
        return empty;
    }
    
    // Step 4: Mark entries with final_mult = 0 as DIST_PADDING
    DEBUG_INFO("Step 4 - Marking entries with final_mult=0 as padding");
    working = working.batched_map(eid, OP_ECALL_TRANSFORM_MARK_ZERO_MULT_PADDING);
    DEBUG_INFO("Step 4 complete, table size=%zu", working.size());
    
    // Debug: Show which entries are marked as padding
    uint32_t padding_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_FINAL_MULT | 
                           DEBUG_COL_FIELD_TYPE | DEBUG_COL_DST_IDX;
    debug_dump_with_mask(working, ("step4_marked_padding_" + table_name).c_str(),
                        ("distexp_step4_padding_" + table_name).c_str(), static_cast<uint32_t>(eid), padding_mask);
    
    // Step 5: Sort to move DIST_PADDING entries to the end
    DEBUG_INFO("Step 5 - Sorting (size=%zu)", working.size());
    { uint64_t t0 = get_current_time_ns();
      working.shuffle_merge_sort(eid, OP_ECALL_COMPARATOR_PADDING_LAST);
      g_de_sort_ns += get_current_time_ns() - t0; }
    DEBUG_INFO("Step 5 complete, table size after sort=%zu", working.size());
    
    // Step 5b: Truncate table to remove excess DIST_PADDING entries
    // This handles cases where output_size < original_size
    if (working.size() > output_size) {
        DEBUG_INFO("Step 5b - Truncating table from %zu to %zu entries", working.size(), output_size);
        Table truncated(working.get_table_name(), working.get_schema());
        for (size_t i = 0; i < output_size; i++) {
            truncated.add_entry(working[i]);
        }
        working = truncated;
        DEBUG_INFO("Step 5b complete, table size after truncation=%zu", working.size());
    }
    
    // Step 6: Add padding entries to reach output_size
    size_t current_size = working.size();
    DEBUG_INFO("Step 6 - Adding padding entries: current_size=%zu, output_size=%zu", current_size, output_size);
    { uint64_t t0 = get_current_time_ns();
      uint8_t table_encryption_status = AssertConsistentEncryption(working);
      size_t padding_needed = output_size - current_size;
      if (padding_needed > 0) {
          working.add_batched_padding(padding_needed, eid, table_encryption_status);
      }
      g_de_padding_ns += get_current_time_ns() - t0; }
    DEBUG_INFO("Step 6 complete, table size after padding=%zu", working.size());
    
    // Step 7b: Debug dump before distribution - shows initial state with non-padding at top
    uint32_t before_dist_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_FINAL_MULT |
                               DEBUG_COL_DST_IDX | DEBUG_COL_FIELD_TYPE;
    debug_dump_with_mask(working, ("step7_before_distribute_" + table_name).c_str(),
                        ("distexp_step7_before_dist_" + table_name).c_str(), static_cast<uint32_t>(eid), before_dist_mask);

    // Steps 7+8: AKS distribute (full hierarchical, all sizes)
    DEBUG_INFO("Steps 7+8 (AKS large) - ocall-driven distribute, output_size=%zu", output_size);
    { uint64_t t0 = get_current_time_ns();
      DistributeManager dm(eid);
      dm.distribute(working, output_size);
      g_de_distribute_ns += get_current_time_ns() - t0; }
    DEBUG_INFO("Steps 7+8 (AKS large) complete, table size=%zu", working.size());

    // Step 9: Expansion phase to fill gaps
    DEBUG_INFO("Step 9 - Expansion phase");
    debug_dump_table(working, ("before_expansion_copy_" + table_name).c_str(),
                    ("distexp_step9a_before_" + table_name).c_str(), static_cast<uint32_t>(eid));
    { uint64_t t0 = get_current_time_ns();
      ExpansionPhase(working, eid);
      g_de_expand_ns += get_current_time_ns() - t0; }
    debug_dump_table(working, ("after_expansion_copy_" + table_name).c_str(),
                    ("distexp_step9b_after_" + table_name).c_str(), static_cast<uint32_t>(eid));
    DEBUG_INFO("Step 9 complete, final table size=%zu", working.size());
    
    // Step 10: Final debug dump showing complete expanded table
    DEBUG_INFO("Step 10 - Final expanded result");
    uint32_t final_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_LOCAL_MULT | 
                         DEBUG_COL_FINAL_MULT | DEBUG_COL_COPY_INDEX | 
                         DEBUG_COL_DST_IDX | DEBUG_COL_FIELD_TYPE;
    debug_dump_with_mask(working, ("final_expanded_" + table_name).c_str(),
                        ("distexp_step10_final_" + table_name).c_str(), static_cast<uint32_t>(eid), final_mask);
    
    return working;
}

size_t DistributeExpand::ComputeOutputSize(const Table& table, sgx_enclave_id_t eid) {
    if (table.size() == 0) {
        return 0;
    }
    
    // Get the last entry's dst_idx + final_mult
    entry_t last_entry = table[table.size() - 1].to_entry_t();
    int32_t output_size = 0;
    
    sgx_status_t status = counted_ecall_obtain_output_size(eid, &output_size, &last_entry);
    if (status != SGX_SUCCESS) {
        DEBUG_ERROR("Failed to obtain output size: %d", status);
        return 0;
    }
    
    return static_cast<size_t>(output_size);
}

void DistributeExpand::DistributePhase(Table& table, size_t output_size, sgx_enclave_id_t eid) {
    if (output_size <= 1) {
        return;
    }

    DEBUG_INFO("Starting distribution phase for %zu entries", output_size);

    // Extract to flat entry_t[] once (336 bytes/entry vs 2256-byte Entry objects).
    // Use flat-array dedup for all passes: unordered_map<flat_index, batch_slot>
    // correctly handles aliased elements (pair(i,i+d) and pair(i-d,i) share slot i)
    // whether they land in the same batch or different batches.
    std::vector<entry_t> flat = table.to_entry_t_vector();
    size_t n = flat.size();

    size_t distance = 1;
    while ((distance << 1) <= output_size) distance <<= 1;

    const size_t pairs_per_ecall = MAX_BATCH_SIZE / 2;

    std::vector<entry_t>        batch_data;
    std::vector<BatchOperation> ops;
    std::unordered_map<size_t, int32_t> flat_idx_map;
    std::vector<size_t>         flat_idx_order;

    batch_data.reserve(MAX_BATCH_SIZE);
    ops.reserve(pairs_per_ecall);
    flat_idx_map.reserve(MAX_BATCH_SIZE);
    flat_idx_order.reserve(MAX_BATCH_SIZE);

    auto flush_flat = [&]() {
        if (ops.empty()) return;
        counted_ecall_batch_dispatcher(eid,
            batch_data.data(), batch_data.size(),
            ops.data(), ops.size(),
            ops.size() * sizeof(BatchOperation),
            OP_ECALL_COMPARATOR_DISTRIBUTE);
        for (size_t k = 0; k < flat_idx_order.size(); k++) {
            flat[flat_idx_order[k]] = batch_data[k];
        }
        flat_idx_map.clear();
        flat_idx_order.clear();
        batch_data.clear();
        ops.clear();
    };

    auto add_pair = [&](size_t left_i, size_t right_i) {
        auto it1 = flat_idx_map.find(left_i);
        int32_t idx1;
        if (it1 == flat_idx_map.end()) {
            idx1 = static_cast<int32_t>(batch_data.size());
            flat_idx_map[left_i] = idx1;
            flat_idx_order.push_back(left_i);
            batch_data.push_back(flat[left_i]);
        } else {
            idx1 = it1->second;
        }

        auto it2 = flat_idx_map.find(right_i);
        int32_t idx2;
        if (it2 == flat_idx_map.end()) {
            idx2 = static_cast<int32_t>(batch_data.size());
            flat_idx_map[right_i] = idx2;
            flat_idx_order.push_back(right_i);
            batch_data.push_back(flat[right_i]);
        } else {
            idx2 = it2->second;
        }

        BatchOperation op;
        op.idx1 = idx1;
        op.idx2 = idx2;
        for (int j = 0; j < MAX_EXTRA_PARAMS; j++) op.extra_params[j] = BATCH_NO_PARAM;
        ops.push_back(op);

        if (ops.size() >= pairs_per_ecall) flush_flat();
    };

    while (distance >= 1) {
        DEBUG_DEBUG("Distribution pass with distance %zu", distance);
        for (size_t i = n - distance; i > 0; i--) {
            add_pair(i - 1, i - 1 + distance);
        }
        if (distance < n) {
            add_pair(0, distance);
        }
        flush_flat();
        distance >>= 1;
    }

    table.from_entry_t_vector(flat);
    DEBUG_INFO("Distribution phase completed");
}

void DistributeExpand::ExpansionPhase(Table& table, sgx_enclave_id_t eid) {
    DEBUG_INFO("Starting expansion phase");
    
    // Linear pass to copy non-empty entries to fill DIST_PADDING slots
    table.batched_linear_pass(eid, OP_ECALL_WINDOW_EXPAND_COPY);
    
    DEBUG_INFO("Expansion phase completed");
}

std::vector<JoinTreeNodePtr> DistributeExpand::GetAllNodes(JoinTreeNodePtr root) {
    std::vector<JoinTreeNodePtr> result;
    
    // Pre-order traversal
    result.push_back(root);
    
    for (auto& child : root->get_children()) {
        auto child_nodes = GetAllNodes(child);
        result.insert(result.end(), child_nodes.begin(), child_nodes.end());
    }
    
    return result;
}