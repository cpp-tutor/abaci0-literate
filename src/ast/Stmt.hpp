#ifndef Stmt_hpp
#define Stmt_hpp

#include "Expr.hpp"
#include "utility/Utility.hpp"
#include <boost/fusion/adapted/struct.hpp>
#include <memory>
#include <vector>
#include <variant>

namespace abaci::ast {

class StmtNode;

using StmtList = std::vector<StmtNode>;

using abaci::utility::Variable;
using abaci::utility::Operator;

struct StmtData {
    virtual ~StmtData() {}
};

class StmtNode {
public:
    StmtNode() = default;
    StmtNode(const StmtNode&) = default;
    StmtNode& operator=(const StmtNode&) = default;
    StmtNode(StmtData *nodeImpl) { data.reset(nodeImpl); }
    const StmtData *get() const { return data.get(); }
private:
    std::shared_ptr<StmtData> data;
};

struct CommentStmt : StmtData {
    std::string comment_string;
};

using PrintList = std::vector<std::variant<ExprNode,Operator>>;

struct PrintStmt : StmtData {
    ExprNode expression;
    PrintList format;
};

struct InitStmt : StmtData {
    Variable name;
    Operator assign;
    ExprNode value;
};

struct AssignStmt : StmtData {
    Variable name;
    Operator assign;
    ExprNode value;
};

struct IfStmt : StmtData {
    ExprNode condition;
    StmtList true_test;
    StmtList false_test;
};

struct WhileStmt : StmtData {
    ExprNode condition;
    StmtList loop_block;
};

struct RepeatStmt : StmtData {
    StmtList loop_block;
    ExprNode condition;
};

struct WhenStmt {
    ExprNode expression;
    StmtList block;
};

using WhenList = std::vector<WhenStmt>;

struct CaseStmt : StmtData {
    ExprNode case_value;
    WhenList matches;
    StmtList unmatched;
};

struct Function : StmtData {
    std::string name;
    std::vector<Variable> parameters;
    StmtList function_body;
};

struct FunctionCall : StmtData {
    std::string name;
    ExprList args;
};

struct ReturnStmt : StmtData {
    ExprNode expression;
    mutable int depth{ -1 };
};

struct ExprFunction : StmtData {
    std::string name;
    std::vector<Variable> parameters;
    Operator to;
    ExprNode expression;
};

using FunctionList = std::vector<Function>;

struct Class : StmtData {
    std::string name;
    std::vector<Variable> variables;
    FunctionList methods;    
};

}

BOOST_FUSION_ADAPT_STRUCT(abaci::ast::CommentStmt, comment_string)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::PrintStmt, expression, format)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::InitStmt, name, assign, value)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::AssignStmt, name, assign, value)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::IfStmt, condition, true_test, false_test)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::WhileStmt, condition, loop_block)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::RepeatStmt, loop_block, condition)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::WhenStmt, expression, block)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::CaseStmt, case_value, matches, unmatched)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::Function, name, parameters, function_body)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::FunctionCall, name, args)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::ReturnStmt, expression)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::ExprFunction, name, parameters, to, expression)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::Class, name, variables, methods)

#endif
