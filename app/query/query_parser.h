#ifndef QUERY_PARSER_H
#define QUERY_PARSER_H

#include <string>
#include <vector>
#include <map>
#include <optional>
#include "query_tokenizer.h"
#include "parsed_query.h"
#include "inequality_parser.h"
#include "condition_merger.h"

/**
 * QueryParser - Parses SQL queries into structured representation
 * 
 * Handles TPC-H style SELECT * queries with multiple tables and join conditions.
 * Uses QueryTokenizer for lexical analysis and InequalityParser for conditions.
 */
class QueryParser {
private:
    QueryTokenizer tokenizer;
    std::vector<Token> tokens;
    size_t current_token_index;
    
    /**
     * Get current token
     */
    const Token& current() const;
    
    /**
     * Get next token without consuming
     */
    const Token& peek() const;
    
    /**
     * Consume current token and move to next
     */
    void consume();
    
    /**
     * Check if current token matches expected type
     */
    bool match(TokenType type) const;
    
    /**
     * Consume token of expected type or throw error
     */
    void expect(TokenType type, const std::string& error_msg);
    
    /**
     * Check if at end of query
     */
    bool is_at_end() const;
    
    /**
     * Parse SELECT clause
     */
    void parse_select(ParsedQuery& query);
    
    /**
     * Parse FROM clause
     */
    void parse_from(ParsedQuery& query);
    
    /**
     * Parse WHERE clause
     */
    void parse_where(ParsedQuery& query);
    
    /**
     * Parse a single condition from WHERE clause
     * Returns the raw condition string
     */
    std::string parse_single_condition();
    
    /**
     * Merge conditions that operate on the same column pairs
     * This handles band joins that are expressed as multiple conditions
     */
    void merge_join_conditions(std::vector<JoinConstraint>& conditions);
    
public:
    /**
     * Parse a SQL query string
     * Returns ParsedQuery structure or throws on error
     */
    ParsedQuery parse(const std::string& sql_query);
    
    /**
     * Reset parser state
     */
    void reset();
};

/**
 * Exception class for parse errors
 */
class ParseException : public std::runtime_error {
public:
    ParseException(const std::string& msg) : std::runtime_error(msg) {}
};

#endif // QUERY_PARSER_H