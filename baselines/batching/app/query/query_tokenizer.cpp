#include "query_tokenizer.h"
#include <sstream>
#include <cctype>
#include <algorithm>

std::string Token::to_string() const {
    std::stringstream ss;
    ss << "[";
    
    switch (type) {
        case TokenType::SELECT: ss << "SELECT"; break;
        case TokenType::FROM: ss << "FROM"; break;
        case TokenType::WHERE: ss << "WHERE"; break;
        case TokenType::AND: ss << "AND"; break;
        case TokenType::EQUALS: ss << "="; break;
        case TokenType::GREATER_EQ: ss << ">="; break;
        case TokenType::GREATER: ss << ">"; break;
        case TokenType::LESS_EQ: ss << "<="; break;
        case TokenType::LESS: ss << "<"; break;
        case TokenType::NOT_EQUALS: ss << "!="; break;
        case TokenType::PLUS: ss << "+"; break;
        case TokenType::MINUS: ss << "-"; break;
        case TokenType::IDENTIFIER: ss << "ID"; break;
        case TokenType::NUMBER: ss << "NUM"; break;
        case TokenType::STAR: ss << "*"; break;
        case TokenType::DOT: ss << "."; break;
        case TokenType::COMMA: ss << ","; break;
        case TokenType::SEMICOLON: ss << ";"; break;
        case TokenType::END_OF_QUERY: ss << "EOF"; break;
        case TokenType::UNKNOWN: ss << "?"; break;
    }
    
    if (!value.empty()) {
        ss << ":" << value;
    }
    ss << "]";
    return ss.str();
}

std::vector<Token> QueryTokenizer::tokenize(const std::string& sql_query) {
    reset();
    query = sql_query;
    current_pos = 0;
    
    while (!is_eof()) {
        skip_whitespace();
        if (is_eof()) break;
        
        size_t token_start = current_pos;
        char ch = peek();
        
        // Handle operators (check two-character operators first)
        if (ch == '>' || ch == '<' || ch == '!' || ch == '=') {
            char first = next();
            if (!is_eof() && peek() == '=') {
                next();  // Consume the '='
                if (first == '>') {
                    tokens.emplace_back(TokenType::GREATER_EQ, ">=", token_start);
                } else if (first == '<') {
                    tokens.emplace_back(TokenType::LESS_EQ, "<=", token_start);
                } else if (first == '!') {
                    tokens.emplace_back(TokenType::NOT_EQUALS, "!=", token_start);
                } else if (first == '=') {
                    // == is just = in SQL
                    tokens.emplace_back(TokenType::EQUALS, "=", token_start);
                }
            } else if (first == '<' && !is_eof() && peek() == '>') {
                next();  // Consume the '>'
                tokens.emplace_back(TokenType::NOT_EQUALS, "<>", token_start);
            } else {
                // Single character operator
                if (first == '>') {
                    tokens.emplace_back(TokenType::GREATER, ">", token_start);
                } else if (first == '<') {
                    tokens.emplace_back(TokenType::LESS, "<", token_start);
                } else if (first == '=') {
                    tokens.emplace_back(TokenType::EQUALS, "=", token_start);
                } else if (first == '!') {
                    tokens.emplace_back(TokenType::UNKNOWN, "!", token_start);
                }
            }
        }
        // Handle arithmetic operators
        else if (ch == '+') {
            next();
            tokens.emplace_back(TokenType::PLUS, "+", token_start);
        }
        else if (ch == '-') {
            next();
            // Could be minus or start of negative number
            // For simplicity, treat as minus operator
            tokens.emplace_back(TokenType::MINUS, "-", token_start);
        }
        // Handle punctuation
        else if (ch == '*') {
            next();
            tokens.emplace_back(TokenType::STAR, "*", token_start);
        }
        else if (ch == '.') {
            next();
            tokens.emplace_back(TokenType::DOT, ".", token_start);
        }
        else if (ch == ',') {
            next();
            tokens.emplace_back(TokenType::COMMA, ",", token_start);
        }
        else if (ch == ';') {
            next();
            tokens.emplace_back(TokenType::SEMICOLON, ";", token_start);
        }
        // Handle numbers
        else if (std::isdigit(ch)) {
            std::string num = read_number();
            tokens.emplace_back(TokenType::NUMBER, num, token_start);
        }
        // Handle identifiers and keywords
        else if (std::isalpha(ch) || ch == '_') {
            std::string id = read_identifier();
            TokenType type = identify_keyword(id);
            tokens.emplace_back(type, id, token_start);
        }
        // Unknown character
        else {
            next();  // Consume it
            tokens.emplace_back(TokenType::UNKNOWN, std::string(1, ch), token_start);
        }
    }
    
    // Add end-of-query token
    tokens.emplace_back(TokenType::END_OF_QUERY, "", current_pos);
    
    return tokens;
}

void QueryTokenizer::skip_whitespace() {
    while (!is_eof() && std::isspace(peek())) {
        next();
    }
    
    // Skip SQL comments (-- style)
    if (!is_eof() && peek() == '-') {
        size_t saved_pos = current_pos;
        next();
        if (!is_eof() && peek() == '-') {
            // Comment found, skip to end of line
            while (!is_eof() && peek() != '\n') {
                next();
            }
            if (!is_eof()) next();  // Skip the newline
            skip_whitespace();  // Recursively skip more whitespace
        } else {
            // Not a comment, restore position
            current_pos = saved_pos;
        }
    }
}

std::string QueryTokenizer::read_identifier() {
    std::string result;
    
    while (!is_eof()) {
        char ch = peek();
        if (std::isalnum(ch) || ch == '_') {
            result += next();
        } else {
            break;
        }
    }
    
    return result;
}

std::string QueryTokenizer::read_number() {
    std::string result;
    bool has_decimal = false;
    
    while (!is_eof()) {
        char ch = peek();
        if (std::isdigit(ch)) {
            result += next();
        } else if (ch == '.' && !has_decimal) {
            // Check if next char is a digit (to distinguish from table.column)
            size_t saved_pos = current_pos;
            next();  // Consume the dot
            if (!is_eof() && std::isdigit(peek())) {
                has_decimal = true;
                result += '.';
            } else {
                // Not a decimal point, restore position
                current_pos = saved_pos;
                break;
            }
        } else {
            break;
        }
    }
    
    return result;
}

TokenType QueryTokenizer::identify_keyword(const std::string& str) {
    // Convert to uppercase for comparison
    std::string upper = str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    if (upper == "SELECT") return TokenType::SELECT;
    if (upper == "FROM") return TokenType::FROM;
    if (upper == "WHERE") return TokenType::WHERE;
    if (upper == "AND") return TokenType::AND;
    
    // Not a keyword, it's an identifier
    return TokenType::IDENTIFIER;
}

char QueryTokenizer::peek() const {
    if (is_eof()) return '\0';
    return query[current_pos];
}

char QueryTokenizer::next() {
    if (is_eof()) return '\0';
    return query[current_pos++];
}

bool QueryTokenizer::is_eof() const {
    return current_pos >= query.length();
}

void QueryTokenizer::reset() {
    tokens.clear();
    current_pos = 0;
    query.clear();
}