#include "CodeGen.hpp"
#include "utility/Utility.hpp"
#include <llvm/IR/Constants.h>

using namespace llvm;

namespace abaci::codegen {

using abaci::ast::ValueCall;
using abaci::ast::ExprNode;
using abaci::ast::ExprList;
using abaci::ast::PrintList;
using abaci::ast::StmtNode;
using abaci::ast::StmtList;
using abaci::ast::CommentStmt;
using abaci::ast::PrintStmt;
using abaci::ast::PrintList;
using abaci::ast::InitStmt;
using abaci::ast::AssignStmt;
using abaci::ast::IfStmt;
using abaci::ast::WhileStmt;
using abaci::ast::RepeatStmt;
using abaci::ast::CaseStmt;
using abaci::ast::WhenStmt;
using abaci::ast::Function;
using abaci::ast::FunctionCall;
using abaci::ast::ReturnStmt;
using abaci::ast::ExprFunction;
using abaci::ast::Class;
using abaci::utility::Operator;

void TypeEvalGen::operator()(const abaci::ast::ExprNode& node) const {
    switch (node.get().index()) {
        case ExprNode::ValueNode:
            push(std::get<AbaciValue>(node.get()).type);
            break;
        case ExprNode::VariableNode:
            push(static_cast<AbaciValue::Type>(environment->getCurrentDefineScope()->getType(std::get<Variable>(node.get()).get()) & AbaciValue::TypeMask));
            break;
        case ExprNode::CallNode: {
            const auto& call = std::get<ValueCall>(node.get());
            switch(cache->getCacheType(call.name)) {
                case Cache::CacheFunction: {
                    const auto& cache_function = cache->getFunction(call.name);
                    std::vector<AbaciValue::Type> types;
                    for (const auto& arg : call.args) {
                        TypeEvalGen expr(environment, cache);
                        expr(arg);
                        types.push_back(expr.get());
                    }
                    auto current_scope = environment->getCurrentDefineScope();
                    environment->beginDefineScope(environment->getGlobalDefineScope());
                    for (auto arg_type = types.begin(); const auto& parameter : cache_function.parameters) {
                        auto type = static_cast<AbaciValue::Type>(*arg_type++ | AbaciValue::Constant);
                        environment->getCurrentDefineScope()->setType(parameter.get(), type);
                    }
                    cache->addFunctionInstantiation(call.name, types, environment);
                    auto return_type = cache->getFunctionInstantiationType(call.name, types);
                    environment->getCurrentDefineScope()->setType("_return", return_type);
                    environment->setCurrentDefineScope(current_scope);
                    push(return_type);
                    break;
                }
                case Cache::CacheClass: {
                    UnexpectedError("Instantiation of classes not yet implemented.");
                    break;
                }
                default:
                    UnexpectedError("Not a function or class.");
            }
            break;
        }
        case ExprNode::ListNode: {
            switch (node.getAssociation()) {
                case ExprNode::Left: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.front());
                    auto type = pop();
                    for (auto iter = ++expr.begin(); iter != expr.end();) {
                        auto op = std::get<Operator>(iter++->get());
                        (*this)(*iter++);
                        type = promote(type, pop());
                        if (type == AbaciValue::Integer && op == Operator::Divide) {
                            type = AbaciValue::Float;
                        }
                    }
                    push(type);
                    break;
                }
                case ExprNode::Right: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.back());
                    auto type = pop();
                    for (auto iter = ++expr.rbegin(); iter != expr.rend();) {
                        ++iter;
                        (*this)(*iter++);
                        type = promote(type, AbaciValue::Float);
                        type = promote(type, pop());
                    }
                    push(type);
                    break;
                }
                case ExprNode::Unary: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.back());
                    auto type = pop();
                    for (auto iter = ++expr.rbegin(); iter != expr.rend();) {
                        auto op = std::get<Operator>(iter++->get());
                        if (op == Operator::Not) {
                            type = AbaciValue::Boolean;
                        }
                    }
                    push(type);
                    break;
                }
                case ExprNode::Boolean: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.front());
                    auto type = pop();
                    if (expr.size() > 1) {
                        type = AbaciValue::Boolean;
                    }
                    push(type);
                    break;
                }
                default:
                    UnexpectedError("Unknown node type.");
            }
            break;
        }
        default:
            UnexpectedError("Bad node type.");
    }
}

AbaciValue::Type TypeEvalGen::promote(AbaciValue::Type a, AbaciValue::Type b) const {
    if (a == b) {
        return a;
    }
    else if (a < AbaciValue::String && b < AbaciValue::String) {
        return std::max(a, b);
    }
    else {
        LogicError("Bad types.");
    }
}

void TypeCodeGen::operator()(const abaci::ast::StmtList& stmts) const {
    if (!stmts.empty()) {
        environment->beginDefineScope();
        for (const auto& stmt : stmts) {
            (*this)(stmt);
        }
        environment->endDefineScope();
    }
}

template<>
void TypeCodeGen::codeGen([[maybe_unused]] const CommentStmt& comment) const {
}

template<>
void TypeCodeGen::codeGen(const PrintStmt& print) const {
    PrintList print_data{ print.expression };
    print_data.insert(print_data.end(), print.format.begin(), print.format.end());
    for (auto field : print_data) {
        switch (field.index()) {
            case 0: {
                TypeEvalGen expr(environment, cache);
                expr(std::get<ExprNode>(field));
                break;
            }
            case 1:
                break;
            default:
                UnexpectedError("Bad field.");
        }
    }
}

template<>
void TypeCodeGen::codeGen(const InitStmt& define) const {
    TypeEvalGen expr(environment, cache);
    expr(define.value);
    auto result = expr.get();
    auto type = static_cast<AbaciValue::Type>(result | ((define.assign == Operator::Equal) ? AbaciValue::Constant : 0));
    environment->getCurrentDefineScope()->setType(define.name.get(), type);
}

template<>
void TypeCodeGen::codeGen(const AssignStmt& assign) const {
    if (!environment->getCurrentDefineScope()->isDefined(assign.name.get())) {
        UnexpectedError("No such variable.");
    }
    else if (environment->getCurrentDefineScope()->getType(assign.name.get()) & AbaciValue::Constant) {
        UnexpectedError("Cannot assign to constant.");
    }
    TypeEvalGen expr(environment, cache);
    expr(assign.value);
    auto result = expr.get();
    if (environment->getCurrentDefineScope()->getType(assign.name.get()) != result) {
        UnexpectedError("Cannot assign to variable with different type.");
    }
}

template<>
void TypeCodeGen::codeGen(const IfStmt& if_stmt) const {
    TypeEvalGen expr(environment, cache);
    expr(if_stmt.condition);
    (*this)(if_stmt.true_test);
    (*this)(if_stmt.false_test);
}

template<>
void TypeCodeGen::codeGen(const WhileStmt& while_stmt) const {
    environment->beginDefineScope();
    TypeEvalGen expr(environment, cache);
    expr(while_stmt.condition);
    for (const auto& stmt : while_stmt.loop_block) {
        (*this)(stmt);
    }
    environment->endDefineScope();
}

template<>
void TypeCodeGen::codeGen(const RepeatStmt& repeat_stmt) const {
    environment->beginDefineScope();
    for (const auto& stmt : repeat_stmt.loop_block) {
        (*this)(stmt);
    }
    TypeEvalGen expr(environment, cache);
    expr(repeat_stmt.condition);
    environment->endDefineScope();
}

template<>
void TypeCodeGen::codeGen(const CaseStmt& case_stmt) const {
    TypeEvalGen expr(environment, cache);
    expr(case_stmt.case_value);
    for (const auto& when : case_stmt.matches) {
        TypeEvalGen expr(environment, cache);
        expr(when.expression);
        (*this)(when.block);
    }
    if (!case_stmt.unmatched.empty()) {
        (*this)(case_stmt.unmatched);
    }
}

template<>
void TypeCodeGen::codeGen(const Function& function) const {
    cache->addFunctionTemplate(function.name, function.parameters, function.function_body);
}

template<>
void TypeCodeGen::codeGen(const FunctionCall& function_call) const {
    const auto& cache_function = cache->getFunction(function_call.name);
    std::vector<AbaciValue::Type> types;
    for (const auto& arg : function_call.args) {
        TypeEvalGen expr(environment, cache);
        expr(arg);
        types.push_back(expr.get());
    }
    auto current_scope = environment->getCurrentDefineScope();
    environment->beginDefineScope(environment->getGlobalDefineScope());
    for (auto arg_type = types.begin(); const auto& parameter : cache_function.parameters) {
        auto type = static_cast<AbaciValue::Type>(*arg_type++ | AbaciValue::Constant);
        environment->getCurrentDefineScope()->setType(parameter.get(), type);
    }
    cache->addFunctionInstantiation(function_call.name, types, environment);
    auto return_type = cache->getFunctionInstantiationType(function_call.name, types);
    environment->getCurrentDefineScope()->setType("_return", return_type);
    environment->setCurrentDefineScope(current_scope);
}

template<>
void TypeCodeGen::codeGen(const ReturnStmt& return_stmt) const {
    TypeEvalGen expr(environment, cache);
    expr(return_stmt.expression);
    auto result = expr.get();
    if (type_is_set && return_type != result) {
        UnexpectedError("Function return type already set to different type.");
    }
    return_type = result;
    type_is_set = true;
    return_stmt.depth = environment->getCurrentDefineScope()->getDepth();
}

template<>
void TypeCodeGen::codeGen(const ExprFunction& expression_function) const {
    auto *return_stmt = new ReturnStmt();
    return_stmt->expression = expression_function.expression;
    return_stmt->depth = 1;
    StmtList function_body{ StmtNode(return_stmt) };
    cache->addFunctionTemplate(expression_function.name, expression_function.parameters, function_body);
}

template<>
void TypeCodeGen::codeGen(const Class& class_template) const {
    std::vector<std::string> method_names;
    for (const auto& method : class_template.methods) {
        method_names.push_back(method.name);
        cache->addFunctionTemplate(class_template.name + '.' + method.name, method.parameters, method.function_body);
    }
    cache->addClassTemplate(class_template.name, class_template.variables, method_names);
}

void TypeCodeGen::operator()(const StmtNode& stmt) const {
    const auto *stmt_data = stmt.get();
    if (dynamic_cast<const CommentStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const CommentStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const PrintStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const PrintStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const InitStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const InitStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const AssignStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const AssignStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const IfStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const IfStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const WhileStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const WhileStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const RepeatStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const RepeatStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const CaseStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const CaseStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const Function*>(stmt_data)) {
        codeGen(dynamic_cast<const Function&>(*stmt_data));
    }
    else if (dynamic_cast<const FunctionCall*>(stmt_data)) {
        codeGen(dynamic_cast<const FunctionCall&>(*stmt_data));
    }
    else if (dynamic_cast<const ReturnStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const ReturnStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const ExprFunction*>(stmt_data)) {
        codeGen(dynamic_cast<const ExprFunction&>(*stmt_data));
    }
    else if (dynamic_cast<const Class*>(stmt_data)) {
        codeGen(dynamic_cast<const Class&>(*stmt_data));
    }
    else {
        UnexpectedError("Bad StmtNode type.");
    }
}

} // namespace abaci::codegen
