#include "query_parser.h"
#include <sstream>
#include <algorithm>
#include <iostream>
#include <set>

ParsedQuery QueryParser::parse(const std::string& sql_query) {
    reset();
    
    // Tokenize the query
    tokens = tokenizer.tokenize(sql_query);
    current_token_index = 0;
    
    ParsedQuery result;
    
    // Parse SELECT clause
    parse_select(result);
    
    // Parse FROM clause
    parse_from(result);
    
    // Parse WHERE clause (optional)
    if (match(TokenType::WHERE)) {
        parse_where(result);
    }
    
    // Check for semicolon or end of query
    if (!is_at_end() && !match(TokenType::SEMICOLON)) {
        // Skip to end or semicolon
        while (!is_at_end() && !match(TokenType::SEMICOLON)) {
            consume();
        }
    }
    
    // Validate the result
    if (!result.is_valid()) {
        throw ParseException("Invalid query structure");
    }
    
    return result;
}

void QueryParser::parse_select(ParsedQuery& query) {
    expect(TokenType::SELECT, "Expected SELECT keyword");
    
    // Handle SELECT *
    if (match(TokenType::STAR)) {
        query.select_columns.push_back("*");
        consume();
    } else {
        // For now, we only support SELECT *
        throw ParseException("Only SELECT * is currently supported");
    }
}

void QueryParser::parse_from(ParsedQuery& query) {
    expect(TokenType::FROM, "Expected FROM keyword");
    
    // Parse comma-separated table list
    do {
        if (!match(TokenType::IDENTIFIER)) {
            throw ParseException("Expected table name in FROM clause");
        }
        
        query.tables.push_back(current().value);
        consume();
        
        // Check for comma
        if (match(TokenType::COMMA)) {
            consume();
        } else {
            break;
        }
    } while (!is_at_end());
}

void QueryParser::parse_where(ParsedQuery& query) {
    expect(TokenType::WHERE, "Expected WHERE keyword");
    
    std::vector<std::string> raw_conditions;
    
    // Parse conditions separated by AND
    do {
        std::string condition = parse_single_condition();
        if (!condition.empty()) {
            raw_conditions.push_back(condition);
        }
        
        // Check for AND
        if (match(TokenType::AND)) {
            consume();
        } else {
            break;
        }
    } while (!is_at_end() && !match(TokenType::SEMICOLON));
    
    // Parse each condition and categorize as join or filter
    std::vector<JoinConstraint> all_join_conditions;
    
    for (const auto& raw_cond : raw_conditions) {
        // Try to parse as join condition
        JoinConstraint join_constraint;
        bool is_join = InequalityParser::parse(raw_cond, join_constraint);
        
        if (is_join) {
            all_join_conditions.push_back(join_constraint);
        } else {
            // Not a join condition, treat as filter
            query.filter_conditions.push_back(raw_cond);
        }
    }
    
    // Merge join conditions on same column pairs
    merge_join_conditions(all_join_conditions);
    query.join_conditions = std::move(all_join_conditions);
}

std::string QueryParser::parse_single_condition() {
    std::stringstream condition_str;
    int paren_depth = 0;
    
    // Collect tokens until we hit AND, semicolon, or end
    while (!is_at_end()) {
        if (paren_depth == 0) {
            if (match(TokenType::AND) || match(TokenType::SEMICOLON)) {
                break;
            }
        }
        
        // Add token value to condition string
        if (!condition_str.str().empty()) {
            condition_str << " ";
        }
        condition_str << current().value;
        
        consume();
    }
    
    return condition_str.str();
}

void QueryParser::merge_join_conditions(std::vector<JoinConstraint>& conditions) {
    // Group conditions by column pairs
    std::map<std::string, std::vector<size_t>> condition_groups;
    
    for (size_t i = 0; i < conditions.size(); i++) {
        const auto& cond = conditions[i];
        // Create a key for the column pair
        std::string key = cond.get_source_table() + "." + cond.get_source_column() + 
                         "~" + cond.get_target_table() + "." + cond.get_target_column();
        condition_groups[key].push_back(i);
    }
    
    // Merge conditions in each group
    std::vector<JoinConstraint> merged_conditions;
    std::set<size_t> processed_indices;
    
    for (const auto& pair : condition_groups) {
        const auto& key = pair.first;
        const auto& indices = pair.second;
        if (indices.size() == 1) {
            // Single condition, no merge needed
            merged_conditions.push_back(conditions[indices[0]]);
            processed_indices.insert(indices[0]);
        } else {
            // Multiple conditions on same columns, merge them
            JoinConstraint merged = conditions[indices[0]];
            processed_indices.insert(indices[0]);
            
            for (size_t i = 1; i < indices.size(); i++) {
                JoinConstraint merge_result;
                bool can_merge = ConditionMerger::merge(merged, conditions[indices[i]], merge_result);
                if (can_merge) {
                    merged = merge_result;
                    processed_indices.insert(indices[i]);
                } else {
                    // Can't merge (conflicting conditions), keep separate
                    // This shouldn't happen in valid queries
                    std::cerr << "Warning: Could not merge conditions on " << key << std::endl;
                }
            }
            
            merged_conditions.push_back(merged);
        }
    }
    
    // Replace with merged conditions
    conditions = std::move(merged_conditions);
}

const Token& QueryParser::current() const {
    if (current_token_index >= tokens.size()) {
        static Token end_token(TokenType::END_OF_QUERY, "", 0);
        return end_token;
    }
    return tokens[current_token_index];
}

const Token& QueryParser::peek() const {
    if (current_token_index + 1 >= tokens.size()) {
        static Token end_token(TokenType::END_OF_QUERY, "", 0);
        return end_token;
    }
    return tokens[current_token_index + 1];
}

void QueryParser::consume() {
    if (!is_at_end()) {
        current_token_index++;
    }
}

bool QueryParser::match(TokenType type) const {
    return current().type == type;
}

void QueryParser::expect(TokenType type, const std::string& error_msg) {
    if (!match(type)) {
        throw ParseException(error_msg + " at position " + 
                           std::to_string(current().position) + 
                           ", got: " + current().to_string());
    }
    consume();
}

bool QueryParser::is_at_end() const {
    return current().type == TokenType::END_OF_QUERY;
}

void QueryParser::reset() {
    tokens.clear();
    current_token_index = 0;
    tokenizer.reset();
}