#ifndef Parse_hpp
#define Parse_hpp

#include "ast/Stmt.hpp"
#include <string>

namespace abaci::parser {

bool parse_block(const std::string& block_str, abaci::ast::StmtList& ast);

bool parse_statement(std::string& stmt_str, abaci::ast::StmtNode& ast);

bool test_statement(const std::string& stmt_str);

} // namespace abaci::parser

#endif
