#ifndef QUERY_TOKENIZER_H
#define QUERY_TOKENIZER_H

#include <string>
#include <vector>
#include <cctype>
#include <algorithm>

/**
 * Token types for SQL parsing
 */
enum class TokenType {
    // Keywords
    SELECT,
    FROM,
    WHERE,
    AND,
    
    // Operators
    EQUALS,      // =
    GREATER_EQ,  // >=
    GREATER,     // >
    LESS_EQ,     // <=
    LESS,        // <
    NOT_EQUALS,  // != or <>
    
    // Arithmetic
    PLUS,        // +
    MINUS,       // -
    
    // Identifiers and literals
    IDENTIFIER,  // Table or column name
    NUMBER,      // Numeric literal
    STAR,        // *
    
    // Punctuation
    DOT,         // .
    COMMA,       // ,
    SEMICOLON,   // ;
    
    // Special
    END_OF_QUERY,
    UNKNOWN
};

/**
 * Token structure
 */
struct Token {
    TokenType type;
    std::string value;
    size_t position;  // Position in original query
    
    Token(TokenType t, const std::string& v, size_t pos)
        : type(t), value(v), position(pos) {}
    
    std::string to_string() const;
};

/**
 * QueryTokenizer - Lexical analyzer for SQL queries
 * 
 * Breaks SQL query into tokens for parsing.
 * Handles TPC-H style queries with joins and conditions.
 */
class QueryTokenizer {
private:
    std::string query;
    size_t current_pos;
    std::vector<Token> tokens;
    
    /**
     * Skip whitespace and comments
     */
    void skip_whitespace();
    
    /**
     * Read an identifier (table/column name or keyword)
     */
    std::string read_identifier();
    
    /**
     * Read a number (integer or decimal)
     */
    std::string read_number();
    
    /**
     * Check if string is a keyword and return its type
     */
    TokenType identify_keyword(const std::string& str);
    
    /**
     * Get next character without advancing position
     */
    char peek() const;
    
    /**
     * Get current character and advance position
     */
    char next();
    
    /**
     * Check if at end of query
     */
    bool is_eof() const;
    
public:
    /**
     * Tokenize a SQL query
     */
    std::vector<Token> tokenize(const std::string& sql_query);
    
    /**
     * Get all tokens
     */
    const std::vector<Token>& get_tokens() const { return tokens; }
    
    /**
     * Reset tokenizer state
     */
    void reset();
};

#endif // QUERY_TOKENIZER_H