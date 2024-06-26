#include "CodeGen.hpp"
#include "utility/Utility.hpp"
#include "parser/Messages.hpp"
#include "parser/Keywords.hpp"
#include <llvm/IR/Constants.h>

using namespace llvm;

namespace abaci::codegen {

using abaci::ast::ValueCall;
using abaci::ast::DataCall;
using abaci::ast::MethodValueCall;
using abaci::ast::TypeConv;
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
using abaci::ast::DataAssignStmt;
using abaci::ast::MethodCall;
using abaci::ast::ExpressionStmt;
using abaci::utility::Operator;

void TypeEvalGen::operator()(const abaci::ast::ExprNode& node) const {
    switch (node.get().index()) {
        case ExprNode::ValueNode:
            push(std::get<AbaciValue>(node.get()).type);
            break;
        case ExprNode::VariableNode: {
            auto name = std::get<Variable>(node.get()).get();
            if (!environment->getCurrentDefineScope()->isDefined(name)) {
                LogicError1(VarNotExist, name);
            }
            auto type = environment->getCurrentDefineScope()->getType(name);
            if (std::holds_alternative<AbaciValue::Type>(type)) {
                push(static_cast<AbaciValue::Type>(std::get<AbaciValue::Type>(type) & AbaciValue::TypeMask));
            }
            else {
                push(type);
            }
            break;
        }
        case ExprNode::CallNode: {
            const auto& call = std::get<ValueCall>(node.get());
            switch(cache->getCacheType(call.name)) {
                case Cache::CacheFunction: {
                    const auto& cache_function = cache->getFunction(call.name);
                    std::vector<Environment::DefineScope::Type> types;
                    for (const auto& arg : call.args) {
                        TypeEvalGen expr(environment, cache);
                        expr(arg);
                        types.push_back(expr.get());
                    }
                    auto current_scope = environment->getCurrentDefineScope();
                    environment->beginDefineScope(environment->getGlobalDefineScope());
                    for (auto arg_type = types.begin(); const auto& parameter : cache_function.parameters) {
                        auto type = *arg_type++;
                        if (std::holds_alternative<AbaciValue::Type>(type)) {
                            type = static_cast<AbaciValue::Type>(std::get<AbaciValue::Type>(type) | AbaciValue::Constant);
                        }
                        environment->getCurrentDefineScope()->setType(parameter.get(), type);
                    }
                    cache->addFunctionInstantiation(call.name, types, environment);
                    auto return_type = cache->getFunctionInstantiationType(call.name, types);
                    environment->getCurrentDefineScope()->setType(RETURN_VAR, return_type);
                    environment->setCurrentDefineScope(current_scope);
                    push(return_type);
                    break;
                }
                case Cache::CacheClass: {
                    Environment::DefineScope::Object object;
                    object.class_name = call.name;
                    for (const auto& arg : call.args) {
                        TypeEvalGen expr(environment, cache);
                        expr(arg);
                        object.object_types.push_back(expr.get());
                    }
                    push(object);
                    break;
                }
                default:
                    LogicError1(CallableNotExist, call.name);
            }
            break;
        }
        case ExprNode::MethodNode: {
            const auto& method_call = std::get<MethodValueCall>(node.get());
            if (!environment->getCurrentDefineScope()->isDefined(method_call.name.get())) {
                LogicError1(VarNotExist, method_call.name.get());
            }
            auto type = environment->getCurrentDefineScope()->getType(method_call.name.get());
            for (const auto& member : method_call.member_list) {
                if (!std::holds_alternative<Environment::DefineScope::Object>(type)) {
                    LogicError0(BadObject);
                }
                auto object = std::get<Environment::DefineScope::Object>(type);
                auto idx = cache->getMemberIndex(cache->getClass(object.class_name), member);
                type = object.object_types.at(idx);
            }
            std::string function_name = std::get<Environment::DefineScope::Object>(type).class_name + '.' + method_call.method;
            const auto& cache_function = cache->getFunction(function_name);
            std::vector<Environment::DefineScope::Type> types;
            for (const auto& arg : method_call.args) {
                TypeEvalGen expr(environment, cache);
                expr(arg);
                types.push_back(expr.get());
            }
            auto current_scope = environment->getCurrentDefineScope();
            environment->beginDefineScope(environment->getGlobalDefineScope());
            environment->getCurrentDefineScope()->setType(THIS_VAR, type);
            for (auto arg_type = types.begin(); const auto& parameter : cache_function.parameters) {
                auto type = *arg_type++;
                if (std::holds_alternative<AbaciValue::Type>(type)) {
                    type = static_cast<AbaciValue::Type>(std::get<AbaciValue::Type>(type) | AbaciValue::Constant);
                }
                environment->getCurrentDefineScope()->setType(parameter.get(), type);
            }
            cache->addFunctionInstantiation(function_name, types, environment);
            auto return_type = cache->getFunctionInstantiationType(function_name, types);
            environment->getCurrentDefineScope()->setType(RETURN_VAR, return_type);
            environment->setCurrentDefineScope(current_scope);
            push(return_type);
            break;
        }
        case ExprNode::DataNode: {
            const auto& data = std::get<DataCall>(node.get());
            if (!environment->getCurrentDefineScope()->isDefined(data.name.get())) {
                LogicError1(VarNotExist, (data.name.get() == THIS_VAR) ? THIS : data.name.get());
            }
            auto type = environment->getCurrentDefineScope()->getType(data.name.get());
            for (const auto& member : data.member_list) {
                if (!std::holds_alternative<Environment::DefineScope::Object>(type)) {
                    LogicError0(BadObject);
                }
                auto object = std::get<Environment::DefineScope::Object>(type);
                auto idx = cache->getMemberIndex(cache->getClass(object.class_name), member);
                type = object.object_types.at(idx);
            }
            push(type);
            break;
        }
        case ExprNode::InputNode:
            push(AbaciValue::String);
            break;
        case ExprNode::ConvNode:
            push(std::get<TypeConv>(node.get()).to_type);
            break;
        case ExprNode::ListNode: {
            switch (node.getAssociation()) {
                case ExprNode::Left: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.front());
                    auto type = pop();
                    for (auto iter = ++expr.begin(); iter != expr.end();) {
                        auto op = std::get<Operator>(iter++->get());
                        (*this)(*iter++);
                        auto new_type = promote(type, pop());
                        if (new_type == AbaciValue::Integer && op == Operator::Divide) {
                            new_type = AbaciValue::Float;
                        }
                        type = new_type;
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
                    UnexpectedError0(BadAssociation);
            }
            break;
        }
        default:
            UnexpectedError0(BadNode);
    }
}

AbaciValue::Type TypeEvalGen::promote(const Environment::DefineScope::Type& type_a, const Environment::DefineScope::Type& type_b) const {
    if (!std::holds_alternative<AbaciValue::Type>(type_a) || !std::holds_alternative<AbaciValue::Type>(type_b)) {
        LogicError0(NoObject);
    }
    auto a = std::get<AbaciValue::Type>(type_a), b = std::get<AbaciValue::Type>(type_b);
    if (a == b) {
        return a;
    }
    else if (a == AbaciValue::Unset || b == AbaciValue::Unset) {
        return AbaciValue::Unset;
    }
    else if (a < AbaciValue::String && b < AbaciValue::String) {
        return std::max(a, b);
    }
    else {
        LogicError0(BadType);
    }
}

void TypeCodeGen::operator()(const abaci::ast::StmtList& stmts) const {
    if (!stmts.empty()) {
        environment->beginDefineScope();
        for (const auto& stmt : stmts) {
            if (dynamic_cast<const ReturnStmt*>(stmt.get()) && &stmt != &stmts.back()) {
                LogicError0(ReturnAtEnd);
            }
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
                UnexpectedError0(BadPrint);
        }
    }
}

template<>
void TypeCodeGen::codeGen(const InitStmt& define) const {
    if (environment->getCurrentDefineScope()->isDefined(define.name.get())) {
        LogicError1(VarExists, define.name.get());
    }
    TypeEvalGen expr(environment, cache);
    expr(define.value);
    auto result = expr.get();
    if (std::holds_alternative<AbaciValue::Type>(result) && define.assign == Operator::Equal) {
        result = static_cast<AbaciValue::Type>(std::get<AbaciValue::Type>(result) | AbaciValue::Constant); 
    }
    environment->getCurrentDefineScope()->setType(define.name.get(), result);
}

template<>
void TypeCodeGen::codeGen(const AssignStmt& assign) const {
    if (!environment->getCurrentDefineScope()->isDefined(assign.name.get())) {
        LogicError1(VarNotExist, assign.name.get());
    }
    else if (std::holds_alternative<AbaciValue::Type>(environment->getCurrentDefineScope()->getType(assign.name.get()))
        && (std::get<AbaciValue::Type>(environment->getCurrentDefineScope()->getType(assign.name.get())) & AbaciValue::Constant)) {
        LogicError1(NoConstantAssign, assign.name.get());
    }
    TypeEvalGen expr(environment, cache);
    expr(assign.value);
    auto result = expr.get(), existing_type = environment->getCurrentDefineScope()->getType(assign.name.get());
    if (std::holds_alternative<AbaciValue::Type>(result) && std::holds_alternative<AbaciValue::Type>(existing_type)) {
        if (std::get<AbaciValue::Type>(result) != std::get<AbaciValue::Type>(existing_type)) {
            LogicError1(VarType, assign.name.get());
        }
    }
    else if (std::holds_alternative<Environment::DefineScope::Object>(result)
        && std::holds_alternative<Environment::DefineScope::Object>(existing_type)) {
        if ((std::get<Environment::DefineScope::Object>(result).class_name
            != std::get<Environment::DefineScope::Object>(existing_type).class_name)
            || !(result == existing_type)) {
            LogicError1(ObjectType, assign.name.get());
        }
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
    if (environment->getCurrentDefineScope()->getEnclosing()) {
        LogicError0(FuncTopLevel);
    }
    cache->addFunctionTemplate(function.name, function.parameters, function.function_body);
}

template<>
void TypeCodeGen::codeGen(const FunctionCall& function_call) const {
    const auto& cache_function = cache->getFunction(function_call.name);
    std::vector<Environment::DefineScope::Type> types;
    for (const auto& arg : function_call.args) {
        TypeEvalGen expr(environment, cache);
        expr(arg);
        types.push_back(expr.get());
    }
    auto current_scope = environment->getCurrentDefineScope();
    environment->beginDefineScope(environment->getGlobalDefineScope());
    for (auto arg_type = types.begin(); const auto& parameter : cache_function.parameters) {
        auto type = *arg_type++;
        if (std::holds_alternative<AbaciValue::Type>(type)) {
            type = static_cast<AbaciValue::Type>(std::get<AbaciValue::Type>(type) | AbaciValue::Constant);
        }
        environment->getCurrentDefineScope()->setType(parameter.get(), type);
    }
    cache->addFunctionInstantiation(function_call.name, types, environment);
    auto return_type = cache->getFunctionInstantiationType(function_call.name, types);
    environment->getCurrentDefineScope()->setType(RETURN_VAR, return_type);
    environment->setCurrentDefineScope(current_scope);
}

template<>
void TypeCodeGen::codeGen(const ReturnStmt& return_stmt) const {
    if (!is_function) {
        LogicError0(ReturnOnlyInFunc);
    }
    TypeEvalGen expr(environment, cache);
    expr(return_stmt.expression);
    auto result = expr.get();
    if (!std::holds_alternative<AbaciValue::Type>(result)
        || (std::holds_alternative<AbaciValue::Type>(result) &&
            (std::get<AbaciValue::Type>(result) != AbaciValue::Unset))) {
        if (type_is_set && !(result == return_type)) {
            LogicError0(FuncTypeSet);
        }
        return_type = result;
        type_is_set = true;
    }
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

template<>
void TypeCodeGen::codeGen(const DataAssignStmt& data_assign) const {
    if (!environment->getCurrentDefineScope()->isDefined(data_assign.name.get())) {
        LogicError1(VarNotExist, data_assign.name.get());
    }
    auto type = environment->getCurrentDefineScope()->getType(data_assign.name.get());
    for (const auto& member : data_assign.member_list) {
        if (!std::holds_alternative<Environment::DefineScope::Object>(type)) {
            LogicError0(BadObject);
        }
        auto object = std::get<Environment::DefineScope::Object>(type);
        auto idx = cache->getMemberIndex(cache->getClass(object.class_name), member);
        type = object.object_types.at(idx);
    }
    TypeEvalGen expr(environment, cache);
    expr(data_assign.value);
    auto result = expr.get();
    if (!(result == type)) {
        LogicError0(DataType);
    }
}

template<>
void TypeCodeGen::codeGen(const MethodCall& method_call) const {
    if (!environment->getCurrentDefineScope()->isDefined(method_call.name.get())) {
        LogicError1(VarNotExist, method_call.name.get());
    }
    auto type = environment->getCurrentDefineScope()->getType(method_call.name.get());
    for (const auto& member : method_call.member_list) {
        if (!std::holds_alternative<Environment::DefineScope::Object>(type)) {
            LogicError0(BadObject);
        }
        auto object = std::get<Environment::DefineScope::Object>(type);
        auto idx = cache->getMemberIndex(cache->getClass(object.class_name), member);
        type = object.object_types.at(idx);
    }
    std::string function_name = std::get<Environment::DefineScope::Object>(type).class_name + '.' + method_call.method;
    const auto& cache_function = cache->getFunction(function_name);
    std::vector<Environment::DefineScope::Type> types;
    for (const auto& arg : method_call.args) {
        TypeEvalGen expr(environment, cache);
        expr(arg);
        types.push_back(expr.get());
    }
    auto current_scope = environment->getCurrentDefineScope();
    environment->beginDefineScope(environment->getGlobalDefineScope());
    environment->getCurrentDefineScope()->setType(THIS_VAR, type);
    for (auto arg_type = types.begin(); const auto& parameter : cache_function.parameters) {
        auto type = *arg_type++;
        if (std::holds_alternative<AbaciValue::Type>(type)) {
            type = static_cast<AbaciValue::Type>(std::get<AbaciValue::Type>(type) | AbaciValue::Constant);
        }
        environment->getCurrentDefineScope()->setType(parameter.get(), type);
    }
    cache->addFunctionInstantiation(function_name, types, environment);
    auto return_type = cache->getFunctionInstantiationType(function_name, types);
    environment->getCurrentDefineScope()->setType(RETURN_VAR, return_type);
    environment->setCurrentDefineScope(current_scope);
}

template<>
void TypeCodeGen::codeGen([[maybe_unused]] const ExpressionStmt& expression_stmt) const {
    LogicError0(NoExpression);
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
    else if (dynamic_cast<const DataAssignStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const DataAssignStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const MethodCall*>(stmt_data)) {
        codeGen(dynamic_cast<const MethodCall&>(*stmt_data));
    }
    else if (dynamic_cast<const ExpressionStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const ExpressionStmt&>(*stmt_data));
    }
    else {
        UnexpectedError0(BadStmtNode);
    }
}

} // namespace abaci::codegen
