#include "align_concat.h"
#include "../join/join_attribute_setter.h"
#include <iostream>
#include <chrono>
#include "debug_util.h"
#include "../utils/counted_ecalls.h"  // Includes both Enclave_u.h and ecall_wrapper.h

// Global variables to track sorting metrics across all concatenations
static double g_total_sort_time = 0.0;
static size_t g_total_sort_ecalls = 0;
static size_t g_sort_operations = 0;
static double g_accumulator_sort_time = 0.0;
static double g_child_sort_time = 0.0;
static size_t g_accumulator_sort_ecalls = 0;
static size_t g_child_sort_ecalls = 0;

// Table debugging functions are declared in debug_util.h

Table AlignConcat::Execute(JoinTreeNodePtr root, sgx_enclave_id_t eid) {
    DEBUG_INFO("Starting Align-Concat Phase...");
    
    // Construct the join result by traversing the tree
    Table result = ConstructJoinResult(root, eid);
    
    DEBUG_INFO("Align-Concat Phase completed. Final result size: %zu", result.size());
    
    return result;
}

Table AlignConcat::ConstructJoinResult(JoinTreeNodePtr root, sgx_enclave_id_t eid) {
    DEBUG_INFO("Constructing join result starting from table: %s",
               root->get_table_name().c_str());

    // Top-down global accumulation (paper Algorithm 4): the result accumulates
    // from the root; each node's OWN expanded table aligns against the GLOBAL
    // result, which already carries every previously attached column and is
    // re-sorted by the edge's parent join column (looked up by name) at full
    // width.
    //
    // The previous implementation folded each child's ENTIRE subtree into a
    // local accumulator before attaching it to the parent.  That mispairs the
    // copies of a "centre" node -- any non-root node with two or more children
    // -- because a locally-folded subtree cannot see how a sibling subtree
    // interleaves the shared parent's copies.  Accumulating one global result
    // top-down, and attaching each node's own table against it, is the fix
    // (matches the verified reference and the single-ecall engine).
    //
    // Attach order: for each parent in pre-order, attach its children in tree
    // order.  Column layout follows attach order (root's columns first), which
    // keeps every join column uniquely findable by its qualified name.
    std::vector<JoinTreeNodePtr> preorder = PreOrderTraversal(root);

    // Result starts as the root's own expanded table.
    Table accumulator = root->get_table();

    for (const auto& node : preorder) {
        for (const auto& child : node->get_children()) {
            DEBUG_INFO("Aligning and concatenating child: %s onto global result",
                       child->get_table_name().c_str());

            // The child's join constraint with THIS parent (node).
            const JoinConstraint& constraint = child->get_constraint_with_parent();

            // Sort the GLOBAL accumulator by the parent's join column (by name);
            // that column already lives in the accumulated layout.
            DEBUG_INFO("Setting accumulator join_attr to: %s",
                       constraint.get_target_column().c_str());
            JoinAttributeSetter::SetJoinAttributesForTable(
                accumulator, constraint.get_target_column(), eid);

            // Attach the child's OWN expanded table (NOT its folded subtree).
            Table child_table = child->get_table();
            DEBUG_INFO("Setting child join_attr to: %s",
                       constraint.get_source_column().c_str());
            JoinAttributeSetter::SetJoinAttributesForTable(
                child_table, constraint.get_source_column(), eid);

            accumulator = AlignAndConcatenate(accumulator, child_table, eid);

            DEBUG_INFO("Accumulator size after adding %s: %zu",
                       child->get_table_name().c_str(), accumulator.size());
        }
    }

    return accumulator;
}

Table AlignConcat::AlignAndConcatenate(const Table& accumulator, 
                                       const Table& child,
                                       sgx_enclave_id_t eid) {
    static int concat_iteration = 0;
    concat_iteration++;
    
    DEBUG_INFO("========================================");
    DEBUG_INFO("=== CONCATENATION OPERATION #%d ===", concat_iteration);
    DEBUG_INFO("Aligning tables: accumulator size=%zu, child size=%zu",
               accumulator.size(), child.size());
    DEBUG_INFO("========================================");
    
    std::string concat_label = "concat" + std::to_string(concat_iteration);
    
    // Step 1: Sort accumulator by join attribute, then other attributes
    DEBUG_INFO("Step 1: Sorting accumulator by join attr, then others");
    Table sorted_accumulator = accumulator;
    
    // Track accumulator sort timing and ecalls
    using Clock = std::chrono::high_resolution_clock;
    auto sort_start = Clock::now();
    size_t before_sort = get_ecall_count();
    
    sorted_accumulator.shuffle_merge_sort(eid, OP_ECALL_COMPARATOR_JOIN_THEN_OTHER);
    
    double acc_sort_time = std::chrono::duration<double>(Clock::now() - sort_start).count();
    size_t acc_sort_ecalls = get_ecall_count() - before_sort;
    DEBUG_INFO("Accumulator sort: %.6fs, %zu ecalls", acc_sort_time, acc_sort_ecalls);
    
    // Step 2: Compute copy indices for child table
    DEBUG_INFO("Step 2: Computing copy indices");
    DEBUG_INFO("Child size before ComputeCopyIndices: %zu rows", child.size());
    Table indexed_child = ComputeCopyIndices(child, eid);
    DEBUG_INFO("Child size after ComputeCopyIndices: %zu rows", indexed_child.size());
    
    // Step 3: Compute alignment keys
    DEBUG_INFO("Step 3: Computing alignment keys");
    Table aligned_child = ComputeAlignmentKeys(indexed_child, eid);
    DEBUG_INFO("Child size after ComputeAlignmentKeys: %zu rows", aligned_child.size());
    
    // Step 4: Sort child by alignment key
    DEBUG_INFO("Step 4: Sorting child by alignment key");
    DEBUG_INFO("Child size before sort: %zu rows", aligned_child.size());
    
    // Track child sort timing and ecalls
    sort_start = Clock::now();
    before_sort = get_ecall_count();
    
    aligned_child.shuffle_merge_sort(eid, OP_ECALL_COMPARATOR_ALIGNMENT_KEY);
    
    DEBUG_INFO("Child size after sort: %zu rows", aligned_child.size());
    
    double child_sort_time = std::chrono::duration<double>(Clock::now() - sort_start).count();
    size_t child_sort_ecalls = get_ecall_count() - before_sort;
    DEBUG_INFO("Child sort: %.6fs, %zu ecalls", child_sort_time, child_sort_ecalls);
    
    // Update global sorting metrics
    g_accumulator_sort_time += acc_sort_time;
    g_child_sort_time += child_sort_time;
    g_accumulator_sort_ecalls += acc_sort_ecalls;
    g_child_sort_ecalls += child_sort_ecalls;
    g_total_sort_time += acc_sort_time + child_sort_time;
    g_total_sort_ecalls += acc_sort_ecalls + child_sort_ecalls;
    g_sort_operations += 2;
    
    // Step 5: Horizontal concatenation using parallel pass
    DEBUG_INFO("Step 5: Horizontal concatenation via parallel pass");
    
    // The result starts as a copy of the sorted accumulator
    Table result = sorted_accumulator;
    
    // CRITICAL DEBUG: Dump both tables RIGHT BEFORE concatenation
    // These are the two tables that will be concatenated horizontally
    DEBUG_INFO("=== TABLES IMMEDIATELY BEFORE HORIZONTAL CONCATENATION ===");
    
    // Dump sorted accumulator (left side of concatenation)
    debug_dump_table(result, ("before_concat_accumulator_" + concat_label).c_str(), 
                    ("align_step5a_before_concat_" + concat_label).c_str(), static_cast<uint32_t>(eid),
                    {META_INDEX, META_ORIG_IDX, META_LOCAL_MULT, META_FINAL_MULT, META_FOREIGN_SUM, META_COPY_INDEX, META_ALIGN_KEY, META_JOIN_ATTR}, true);
    
    // Dump aligned child (right side of concatenation)  
    debug_dump_table(aligned_child, ("before_concat_child_" + concat_label).c_str(),
                    ("align_step5b_before_concat_" + concat_label).c_str(), static_cast<uint32_t>(eid),
                    {META_INDEX, META_ORIG_IDX, META_LOCAL_MULT, META_FINAL_MULT, META_FOREIGN_SUM, META_COPY_INDEX, META_ALIGN_KEY, META_JOIN_ATTR}, true);
    
    DEBUG_INFO("Table sizes - Accumulator: %zu rows, Child: %zu rows", result.size(), aligned_child.size());
    
    // Add immediate check for size mismatch
    if (result.size() != aligned_child.size()) {
        DEBUG_ERROR("Size mismatch before parallel_pass - Accumulator: %zu rows, Child: %zu rows", 
                    result.size(), aligned_child.size());
    }
    
    DEBUG_INFO("These two tables will now be concatenated horizontally (parallel_pass)");
    
    // Debug: Print first few entries before concatenation
    if (result.size() > 0 && aligned_child.size() > 0) {
        DEBUG_INFO("Before concat - first accumulator entry:");
        DEBUG_INFO("  original_index=%d, join_attr=%d, final_mult=%d",
                   result[0].original_index, result[0].join_attr, result[0].final_mult);
        DEBUG_INFO("Before concat - first child entry:");
        DEBUG_INFO("  original_index=%d, join_attr=%d, alignment_key=%d, copy_index=%d",
                   aligned_child[0].original_index, aligned_child[0].join_attr,
                   aligned_child[0].alignment_key, aligned_child[0].copy_index);
    }
    
    // Get attribute counts from table schemas
    // Tables know their actual column count through their schema
    int32_t left_attr_count = static_cast<int32_t>(result.get_schema().size());
    int32_t right_attr_count = static_cast<int32_t>(aligned_child.get_schema().size());
    
    DEBUG_INFO("Attribute counts from schemas - left: %d, right: %d", 
               left_attr_count, right_attr_count);
    
    // Pass attribute counts as extra parameters
    int32_t concat_params[MAX_EXTRA_PARAMS] = {left_attr_count, right_attr_count, 0, 0};
    
    // Use parallel_pass to concatenate attributes from aligned_child
    result.batched_parallel_pass(aligned_child, eid, OP_ECALL_CONCAT_ATTRIBUTES, concat_params);
    
    // Update the result table's schema to include columns from both tables
    std::vector<std::string> combined_schema = result.get_schema();
    std::vector<std::string> child_schema = aligned_child.get_schema();
    
    // Append child schema to the combined schema
    combined_schema.insert(combined_schema.end(), child_schema.begin(), child_schema.end());
    result.set_schema(combined_schema);
    
    DEBUG_INFO("Updated schema after concatenation: %zu columns", combined_schema.size());
    
    // Debug: Dump final result - show concatenated attributes WITH ALL COLUMNS
    debug_dump_table(result, ("final_result_" + concat_label).c_str(), 
                    ("align_step5_" + concat_label).c_str(), static_cast<uint32_t>(eid),
                    {META_INDEX, META_ORIG_IDX, META_LOCAL_MULT, META_FINAL_MULT, META_FOREIGN_SUM, META_COPY_INDEX, META_ALIGN_KEY, META_JOIN_ATTR}, true);
    
    DEBUG_INFO("========================================");
    DEBUG_INFO("=== END CONCATENATION #%d ===", concat_iteration);
    DEBUG_INFO("Result size: %zu", result.size());
    DEBUG_INFO("========================================\n");
    
    return result;
}

Table AlignConcat::ComputeCopyIndices(const Table& table, sgx_enclave_id_t eid) {
    if (table.size() == 0) {
        return table;
    }
    
    DEBUG_DEBUG("Computing copy indices for %zu entries", table.size());
    
    // Initialize copy_index to 0 for all entries
    Table result = table.batched_map(eid, OP_ECALL_TRANSFORM_INIT_COPY_INDEX);
    
    // Linear pass to compute copy indices
    // Same original_index -> increment
    // Different original_index -> reset to 0
    result.batched_linear_pass(eid, OP_ECALL_WINDOW_UPDATE_COPY_INDEX);
    
    DEBUG_DEBUG("Copy indices computed");
    
    return result;
}

Table AlignConcat::ComputeAlignmentKeys(const Table& table, sgx_enclave_id_t eid) {
    DEBUG_DEBUG("Computing alignment keys for %zu entries", table.size());
    
    // Apply transformation to compute alignment_key = foreign_sum + (copy_index / local_mult)
    Table result = table.batched_map(eid, OP_ECALL_TRANSFORM_COMPUTE_ALIGNMENT_KEY);
    
    DEBUG_DEBUG("Alignment keys computed");
    
    return result;
}

std::vector<JoinTreeNodePtr> AlignConcat::PreOrderTraversal(JoinTreeNodePtr root) {
    std::vector<JoinTreeNodePtr> result;
    
    // Visit current node first (pre-order)
    result.push_back(root);
    
    // Then visit children
    for (const auto& child : root->get_children()) {
        auto child_nodes = PreOrderTraversal(child);
        result.insert(result.end(), child_nodes.begin(), child_nodes.end());
    }
    
    return result;
}

void AlignConcat::GetSortingMetrics(double& total_time, size_t& total_ecalls,
                                    double& acc_time, double& child_time,
                                    size_t& acc_ecalls, size_t& child_ecalls) {
    total_time = g_total_sort_time;
    total_ecalls = g_total_sort_ecalls;
    acc_time = g_accumulator_sort_time;
    child_time = g_child_sort_time;
    acc_ecalls = g_accumulator_sort_ecalls;
    child_ecalls = g_child_sort_ecalls;
}

void AlignConcat::ResetSortingMetrics() {
    g_total_sort_time = 0.0;
    g_total_sort_ecalls = 0;
    g_sort_operations = 0;
    g_accumulator_sort_time = 0.0;
    g_child_sort_time = 0.0;
    g_accumulator_sort_ecalls = 0;
    g_child_sort_ecalls = 0;
}