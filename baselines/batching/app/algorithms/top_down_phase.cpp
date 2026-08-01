#include "top_down_phase.h"
#include "../join/join_attribute_setter.h"
#include <iostream>
#include "debug_util.h"
#include "../utils/counted_ecalls.h"  // Use counted ecalls

// Debug functions are declared in debug_util.h

void TopDownPhase::Execute(JoinTreeNodePtr root, sgx_enclave_id_t eid) {
    
    // Step 1: Initialize ONLY root table with final_mult = local_mult
    InitializeRootTable(root, eid);
    
    // Step 2: Pre-order traversal (root to leaves)
    auto nodes = PreOrderTraversal(root);
    
    // Step 3: Process each node (skip root as it's already initialized)
    for (size_t i = 1; i < nodes.size(); i++) {
        auto& node = nodes[i];
        auto parent = node->get_parent();
        
        if (parent) {
            
            // Compute foreign multiplicities from parent to child
            ComputeForeignMultiplicities(
                parent->get_table(),
                node->get_table(),
                node->get_constraint_with_parent(),
                eid);
        }
    }
    
    // Final debug dump of all tables (similar to bottom-up step 12)
    DEBUG_INFO("Top-Down Phase final - dumping all tables with foreign_sum");
    
    // Collect all nodes in the tree
    std::vector<JoinTreeNodePtr> all_nodes;
    std::function<void(JoinTreeNodePtr)> collect = [&](JoinTreeNodePtr n) {
        all_nodes.push_back(n);
        for (auto& child : n->get_children()) {
            collect(child);
        }
    };
    collect(root);
    
    for (const auto& node : all_nodes) {
        // Dump with foreign_sum to verify top-down computation
        uint32_t mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_LOCAL_MULT | 
                       DEBUG_COL_FINAL_MULT | DEBUG_COL_FOREIGN_SUM |
                       DEBUG_COL_FIELD_TYPE | DEBUG_COL_EQUALITY_TYPE | 
                       DEBUG_COL_JOIN_ATTR;
        std::string step_name = "topdown_step12_final_" + node->get_table_name();
        debug_dump_with_mask(node->get_table(), node->get_table_name().c_str(), step_name.c_str(), static_cast<uint32_t>(eid), mask);
    }
}

void TopDownPhase::InitializeRootTable(JoinTreeNodePtr node, sgx_enclave_id_t eid) {
    // Initialize root table only: final_mult = local_mult
    // Initialize root table
    node->set_table(node->get_table().batched_map(eid, OP_ECALL_TRANSFORM_INIT_FINAL_MULT));
}

void TopDownPhase::InitializeForeignFields(JoinTreeNodePtr node, sgx_enclave_id_t eid) {
    // Initialize foreign-related fields to 0
    node->set_table(node->get_table().batched_map(eid, OP_ECALL_TRANSFORM_INIT_FOREIGN_TEMPS));
}

Table TopDownPhase::CombineTableForForeign(
    const Table& parent,
    const Table& child,
    const JoinConstraint& constraint,
    sgx_enclave_id_t eid) {
    
    DEBUG_INFO("CombineTableForForeign: parent=%zu entries, child=%zu entries", 
               parent.size(), child.size());
    
    // In top-down, parent is SOURCE and child is TARGET
    // But the stored constraint has child as SOURCE and parent as TARGET
    // So we need to reverse the constraint
    JoinConstraint reversed_constraint = constraint.reverse();
    
    // Get reversed constraint parameters
    auto params = reversed_constraint.get_params();
    int32_t dev1 = params.deviation1;
    int32_t dev2 = params.deviation2;
    equality_type_t eq1 = params.equality1;
    equality_type_t eq2 = params.equality2;
    
    DEBUG_INFO("Original constraint: dev1=%d, dev2=%d", 
               constraint.get_params().deviation1, constraint.get_params().deviation2);
    DEBUG_INFO("Reversed constraint: dev1=%d, dev2=%d", dev1, dev2);
    
    // Transform parent entries to SOURCE type (parent provides multiplicities)
    DEBUG_INFO("Transforming parent entries to SOURCE type");
    Table source_entries = parent.batched_map(eid, OP_ECALL_TRANSFORM_TO_SOURCE);
    
    // Transform child entries to START boundaries (child receives multiplicities)
    DEBUG_INFO("Transforming child entries to START boundaries");
    int32_t start_params[] = {dev1, eq1};
    Table start_entries = child.batched_map(eid, OP_ECALL_TRANSFORM_TO_START, start_params);
    
    // Transform child entries to END boundaries
    DEBUG_INFO("Transforming child entries to END boundaries");
    int32_t end_params[] = {dev2, eq2};
    Table end_entries = child.batched_map(eid, OP_ECALL_TRANSFORM_TO_END, end_params);
    
    // Combine all three tables
    // Use parent schema for combined table (all three should have same schema)
    Table combined("combined_foreign", parent.get_schema().empty() ? child.get_schema() : parent.get_schema());
    
    // Add source entries (parent)
    for (const auto& entry : source_entries) {
        combined.add_entry(entry);
    }
    
    // Add start entries (child)
    for (const auto& entry : start_entries) {
        combined.add_entry(entry);
    }
    
    // Add end entries (child)
    for (const auto& entry : end_entries) {
        combined.add_entry(entry);
    }
    
    DEBUG_INFO("Combined table created with %zu entries", combined.size());
    
    return combined;
}

void TopDownPhase::ComputeForeignMultiplicities(
    Table& parent,
    Table& child,
    const JoinConstraint& constraint,
    sgx_enclave_id_t eid) {
    
    // Step 0: Set join_attr for both parent and child based on their join columns
    DEBUG_INFO("Setting join_attr for parent using column: %s", constraint.get_target_column().c_str());
    JoinAttributeSetter::SetJoinAttributesForTable(parent, constraint.get_target_column(), eid);
    
    DEBUG_INFO("Setting join_attr for child using column: %s", constraint.get_source_column().c_str());
    JoinAttributeSetter::SetJoinAttributesForTable(child, constraint.get_source_column(), eid);
    
    // Step 1: Create combined table for foreign computation
    DEBUG_INFO("Creating combined table for foreign computation");
    Table combined = CombineTableForForeign(parent, child, constraint, eid);
    
    // Debug: Dump combined table after creation
    uint32_t combined_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_FIELD_TYPE | 
                            DEBUG_COL_JOIN_ATTR | DEBUG_COL_LOCAL_MULT | DEBUG_COL_FINAL_MULT |
                            DEBUG_COL_EQUALITY_TYPE;
    debug_dump_with_mask(combined, "combined_foreign", "topdown_step1_combined", static_cast<uint32_t>(eid), combined_mask);
    
    // Step 2: Initialize foreign temporary fields
    DEBUG_INFO("Initializing foreign temporary fields");
    combined = combined.batched_map(eid, OP_ECALL_TRANSFORM_INIT_FOREIGN_TEMPS);
    
    // Debug: Dump after initializing foreign temps
    uint32_t foreign_init_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_FIELD_TYPE |
                                DEBUG_COL_JOIN_ATTR | DEBUG_COL_LOCAL_MULT | DEBUG_COL_FINAL_MULT |
                                DEBUG_COL_EQUALITY_TYPE | DEBUG_COL_FOREIGN_SUM | 
                                DEBUG_COL_LOCAL_WEIGHT;
    debug_dump_with_mask(combined, "foreign_temps_init", "topdown_step2_init_temps", static_cast<uint32_t>(eid), foreign_init_mask);
    
    // Step 3: Sort by join attribute
    DEBUG_INFO("Sorting by join attribute");
    combined.shuffle_merge_sort(eid, OP_ECALL_COMPARATOR_JOIN_ATTR);
    
    // Debug: Dump after sorting by join attribute
    debug_dump_with_mask(combined, "sorted_by_join", "topdown_step3_sorted", static_cast<uint32_t>(eid), foreign_init_mask);
    
    // Step 4: Compute foreign cumulative sums and weights
    DEBUG_INFO("Computing foreign cumulative sums");
    combined.batched_linear_pass(eid, OP_ECALL_WINDOW_COMPUTE_FOREIGN_SUM);
    
    // Debug: Dump after computing foreign cumulative sums
    uint32_t foreign_sum_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_FIELD_TYPE |
                               DEBUG_COL_JOIN_ATTR | DEBUG_COL_LOCAL_MULT | DEBUG_COL_FINAL_MULT |
                               DEBUG_COL_EQUALITY_TYPE | DEBUG_COL_FOREIGN_SUM | 
                               DEBUG_COL_LOCAL_WEIGHT;
    debug_dump_with_mask(combined, "foreign_sum", "topdown_step4_cumsum", static_cast<uint32_t>(eid), foreign_sum_mask);
    
    // Step 5: Sort for pairwise processing
    DEBUG_INFO("Sorting for pairwise processing");
    combined.shuffle_merge_sort(eid, OP_ECALL_COMPARATOR_PAIRWISE);
    
    // Debug: Dump after pairwise sort
    debug_dump_with_mask(combined, "sorted_pairwise", "topdown_step5_pairwise", static_cast<uint32_t>(eid), foreign_sum_mask);
    
    // Step 6: Compute foreign intervals
    DEBUG_INFO("Computing foreign intervals");
    combined.batched_linear_pass(eid, OP_ECALL_WINDOW_COMPUTE_FOREIGN_INTERVAL);
    
    // Debug: Dump after computing foreign intervals
    uint32_t foreign_interval_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_FIELD_TYPE |
                                     DEBUG_COL_JOIN_ATTR | DEBUG_COL_LOCAL_MULT | DEBUG_COL_FINAL_MULT |
                                     DEBUG_COL_EQUALITY_TYPE | DEBUG_COL_FOREIGN_SUM |
                                     DEBUG_COL_FOREIGN_INTERVAL | 
                                     DEBUG_COL_LOCAL_WEIGHT;
    debug_dump_with_mask(combined, "foreign_intervals", "topdown_step6_intervals", static_cast<uint32_t>(eid), foreign_interval_mask);
    
    // Step 7: Sort END entries first to extract computed intervals
    DEBUG_INFO("Sorting END entries first");
    combined.shuffle_merge_sort(eid, OP_ECALL_COMPARATOR_END_FIRST);
    
    // Debug: Dump after END-first sort
    debug_dump_with_mask(combined, "sorted_end_first", "topdown_step7_end_first", static_cast<uint32_t>(eid), foreign_interval_mask);
    
    // Step 8: Truncate to child size
    DEBUG_INFO("Truncating to %zu entries (child size)", child.size());
    Table truncated("truncated_foreign", child.get_schema());
    
    for (size_t i = 0; i < child.size() && i < combined.size(); i++) {
        truncated.add_entry(combined[i]);
    }
    
    // Debug: Dump truncated table (should contain END entries with intervals)
    uint32_t truncated_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_FIELD_TYPE |
                             DEBUG_COL_JOIN_ATTR | DEBUG_COL_LOCAL_MULT | DEBUG_COL_FINAL_MULT |
                             DEBUG_COL_EQUALITY_TYPE | DEBUG_COL_FOREIGN_SUM |
                             DEBUG_COL_FOREIGN_INTERVAL |
                             DEBUG_COL_LOCAL_WEIGHT;
    debug_dump_with_mask(truncated, "truncated_foreign", "topdown_step8_truncated", static_cast<uint32_t>(eid), truncated_mask);
    
    // Step 9: Update child's final multiplicities
    DEBUG_INFO("Updating child's final multiplicities");
    
    // Debug: Dump child table before update
    uint32_t child_mask = DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_FIELD_TYPE |
                         DEBUG_COL_JOIN_ATTR | DEBUG_COL_LOCAL_MULT | 
                         DEBUG_COL_FINAL_MULT | DEBUG_COL_EQUALITY_TYPE;
    debug_dump_with_mask(child, "child_before_update", "topdown_step9a_before", static_cast<uint32_t>(eid), child_mask);
    
    // Debug: Check field types before update
    DEBUG_INFO("Before parallel_pass - checking first entry field types");
    if (child.size() > 0) {
        DEBUG_INFO("  Child[0] field_type=%d, equality_type=%d",
                   child[0].field_type, child[0].equality_type);
    }
    
    truncated.batched_parallel_pass(child, eid, OP_ECALL_UPDATE_TARGET_FINAL_MULTIPLICITY);
    
    // Debug: Check field types after update
    DEBUG_INFO("After parallel_pass - checking first entry field types");
    if (child.size() > 0) {
        DEBUG_INFO("  Child[0] field_type=%d, equality_type=%d",
                   child[0].field_type, child[0].equality_type);
    }
    
    // Debug: Dump child table after update
    debug_dump_with_mask(child, "child_after_update", "topdown_step9b_after", static_cast<uint32_t>(eid), child_mask);
    
    DEBUG_INFO("Child final multiplicities updated");
}

std::vector<JoinTreeNodePtr> TopDownPhase::PreOrderTraversal(JoinTreeNodePtr root) {
    std::vector<JoinTreeNodePtr> result;
    
    // Visit current node first (pre-order)
    result.push_back(root);
    
    // Then visit children recursively
    for (auto& child : root->get_children()) {
        auto child_nodes = PreOrderTraversal(child);
        result.insert(result.end(), child_nodes.begin(), child_nodes.end());
    }
    
    return result;
}