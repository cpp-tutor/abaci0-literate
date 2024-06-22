#include "CodeGen.hpp"
#include "utility/Utility.hpp"
#include "parser/Messages.hpp"
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
using abaci::ast::DataAssignStmt;
using abaci::ast::MethodCall;
using abaci::ast::ExpressionStmt;
using abaci::utility::Operator;

void StmtCodeGen::operator()(const StmtList& stmts, BasicBlock *exit_block) const {
    if (!stmts.empty()) {
        Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
        Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
        environment->beginDefineScope();
        builder.CreateCall(module.getFunction("beginScope"), { typed_environment_ptr });
        for (const auto& stmt : stmts) {
            (*this)(stmt);
        }
        if (!dynamic_cast<const ReturnStmt*>(stmts.back().get())) {
            builder.CreateCall(module.getFunction("endScope"), { typed_environment_ptr });
            if (exit_block) {
                builder.CreateBr(exit_block);
            }
        }
        environment->endDefineScope();
    }
    else {
        if (exit_block) {
            builder.CreateBr(exit_block);
        }
    }
}

template<>
void StmtCodeGen::codeGen([[maybe_unused]] const CommentStmt& comment) const {
}

template<>
void StmtCodeGen::codeGen(const PrintStmt& print) const {
    PrintList print_data{ print.expression };
    print_data.insert(print_data.end(), print.format.begin(), print.format.end());
    for (auto field : print_data) {
        switch (field.index()) {
            case 0: {
                ExprCodeGen expr(jit);
                expr(std::get<ExprNode>(field));
                auto result = expr.get();
                auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
                builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
                builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), environmentTypeToType(result.second)), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
                builder.CreateCall(module.getFunction("printValue"), { abaci_value });
                break;
            }
            case 1: {
                auto oper = std::get<Operator>(field);
                switch (oper) {
                    case Operator::Comma:
                        builder.CreateCall(module.getFunction("printComma"));
                        break;
                    case Operator::SemiColon:
                        break;
                    default:
                        UnexpectedError0(BadOperator);
                }
                break;
            }
            default:
                UnexpectedError0(BadPrint);
                break;
        }
    }
    if (!print_data.empty() && print_data.back().index() == 1) {
        if (std::get<Operator>(print_data.back()) != Operator::SemiColon && std::get<Operator>(print_data.back()) != Operator::Comma) {
            builder.CreateCall(module.getFunction("printLn"));
        }
    }
    else {
        builder.CreateCall(module.getFunction("printLn"));
    }
}

template<>
void StmtCodeGen::codeGen(const InitStmt& define) const {
    Constant *name = ConstantDataArray::getString(module.getContext(), define.name.get().c_str());
    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
    builder.CreateStore(name, str);
    ExprCodeGen expr(jit);
    expr(define.value);
    auto result = expr.get();
    Environment::DefineScope::Type type;
    if (std::holds_alternative<AbaciValue::Type>(result.second) && define.assign == Operator::Equal) {
        type = static_cast<AbaciValue::Type>(std::get<AbaciValue::Type>(result.second) | AbaciValue::Constant); 
    }
    else {
        type = result.second;
    }
    if (environment->getCurrentDefineScope()->getEnclosing()) {
        environment->getCurrentDefineScope()->setType(define.name.get(), type);
    }
    auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
    builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), environmentTypeToType(type)), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
    builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), abaci_value, ConstantInt::get(builder.getInt1Ty(), true) });
}

template<>
void StmtCodeGen::codeGen(const AssignStmt& assign) const {
    if (!environment->getCurrentDefineScope()->isDefined(assign.name.get())) {
        UnexpectedError1(VarNotExist, assign.name.get());
    }
    else if (auto type = environment->getCurrentDefineScope()->getType(assign.name.get());
        std::holds_alternative<AbaciValue::Type>(type) && (std::get<AbaciValue::Type>(type) & AbaciValue::Constant)) {
        UnexpectedError1(NoConstantAssign, assign.name.get());
    }
    Constant *name = ConstantDataArray::getString(module.getContext(), assign.name.get().c_str());
    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
    builder.CreateStore(name, str);
    ExprCodeGen expr(jit);
    expr(assign.value);
    auto result = expr.get();
    if (!(environment->getCurrentDefineScope()->getType(assign.name.get()) == result.second)) {
        LogicError1(VarType, assign.name.get());
    }
    auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
    builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), environmentTypeToType(result.second)), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
    builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), abaci_value, ConstantInt::get(builder.getInt1Ty(), false) });
}

template<>
void StmtCodeGen::codeGen(const IfStmt& if_stmt) const {
    ExprCodeGen expr(jit);
    expr(if_stmt.condition);
    auto result = expr.get();
    Value *condition;
    if (environmentTypeToType(result.second) == AbaciValue::Boolean) {
        condition = result.first;
    }
    else {
        condition = expr.toBoolean(result);
    }
    BasicBlock *true_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    BasicBlock *false_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    BasicBlock *merge_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    builder.CreateCondBr(condition, true_block, false_block);
    builder.SetInsertPoint(true_block);
    (*this)(if_stmt.true_test, merge_block);
    builder.SetInsertPoint(false_block);
    (*this)(if_stmt.false_test, merge_block);
    builder.SetInsertPoint(merge_block);
}

template<>
void StmtCodeGen::codeGen(const WhileStmt& while_stmt) const {
    BasicBlock *pre_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    BasicBlock *loop_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    BasicBlock *post_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
    environment->beginDefineScope();
    builder.CreateCall(module.getFunction("beginScope"), { typed_environment_ptr });
    builder.CreateBr(pre_block);
    builder.SetInsertPoint(pre_block);
    ExprCodeGen expr(jit);
    expr(while_stmt.condition);
    auto result = expr.get();
    Value *condition;
    if (environmentTypeToType(result.second) == AbaciValue::Boolean) {
        condition = result.first;
    }
    else {
        condition = expr.toBoolean(result);
    }
    builder.CreateCondBr(condition, loop_block, post_block);
    builder.SetInsertPoint(loop_block);
    for (const auto& stmt : while_stmt.loop_block) {
        (*this)(stmt);
    }
    builder.CreateBr(pre_block);
    builder.SetInsertPoint(post_block);
    builder.CreateCall(module.getFunction("endScope"), { typed_environment_ptr });
    environment->endDefineScope();
}

template<>
void StmtCodeGen::codeGen(const RepeatStmt& repeat_stmt) const {
    BasicBlock *loop_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    BasicBlock *post_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
    environment->beginDefineScope();
    builder.CreateCall(module.getFunction("beginScope"), { typed_environment_ptr });
    builder.CreateBr(loop_block);
    builder.SetInsertPoint(loop_block);
    for (const auto& stmt : repeat_stmt.loop_block) {
        (*this)(stmt);
    }
    ExprCodeGen expr(jit);
    expr(repeat_stmt.condition);
    auto result = expr.get();
    Value *condition;
    if (environmentTypeToType(result.second) == AbaciValue::Boolean) {
        condition = result.first;
    }
    else {
        condition = expr.toBoolean(result);
    }
    builder.CreateCondBr(condition, post_block, loop_block);
    builder.SetInsertPoint(post_block);
    builder.CreateCall(module.getFunction("endScope"), { typed_environment_ptr });
    environment->endDefineScope();
}

template<>
void StmtCodeGen::codeGen(const CaseStmt& case_stmt) const {
    std::vector<BasicBlock*> case_blocks(case_stmt.matches.size() * 2 + 1 + !case_stmt.unmatched.empty());
    for (auto& block : case_blocks) {
        block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    }
    ExprCodeGen expr(jit);
    expr(case_stmt.case_value);
    auto result = expr.get();
    builder.CreateBr(case_blocks.front());
    for (int block_number = 0; const auto& when : case_stmt.matches) {
        builder.SetInsertPoint(case_blocks.at(block_number * 2));
        auto match_result = result;
        ExprCodeGen expr(jit);
        expr(when.expression);
        auto when_result = expr.get();
        Value *is_match;
        switch (expr.promote(when_result, match_result)) {
            case AbaciValue::Boolean:
            case AbaciValue::Integer:
                is_match = builder.CreateICmpEQ(when_result.first, match_result.first);
                break;
            case AbaciValue::Float:
                is_match = builder.CreateFCmpOEQ(when_result.first, match_result.first);
                break;
            case AbaciValue::Complex: {
                Value *real_value1 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), when_result.first, 0));
                Value *imag_value1 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), when_result.first, 1));
                Value *real_value2 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), match_result.first, 0));
                Value *imag_value2 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), match_result.first, 1));
                is_match = builder.CreateAnd(builder.CreateFCmpOEQ(real_value1, real_value2), builder.CreateFCmpOEQ(imag_value1, imag_value2));
                break;
            }
            case AbaciValue::String: {
                Value *str1_ptr = builder.CreateLoad(builder.getInt8PtrTy(), builder.CreateStructGEP(jit.getNamedType("struct.String"), when_result.first, 0));
                Value *str2_ptr = builder.CreateLoad(builder.getInt8PtrTy(), builder.CreateStructGEP(jit.getNamedType("struct.String"), match_result.first, 0));
                is_match = builder.CreateICmpEQ(ConstantInt::get(builder.getInt32Ty(), 0),
                    builder.CreateCall(module.getFunction("strcmp"), { str1_ptr, str2_ptr }));
                break;
            }
            default:
                LogicError0(BadType);
        }
        builder.CreateCondBr(is_match, case_blocks.at(block_number * 2 + 1), case_blocks.at(block_number * 2 + 2));
        builder.SetInsertPoint(case_blocks.at(block_number * 2 + 1));
        (*this)(when.block, case_blocks.back());
        ++block_number;
    }
    if (!case_stmt.unmatched.empty()) {
        builder.SetInsertPoint(case_blocks.at(case_blocks.size() - 2));
        (*this)(case_stmt.unmatched, case_blocks.back());
    }
    builder.SetInsertPoint(case_blocks.back());
}

template<>
void StmtCodeGen::codeGen([[maybe_unused]] const Function& function) const {
}

template<>
void StmtCodeGen::codeGen(const FunctionCall& function_call) const {
    const auto& cache_function = jit.getCache()->getFunction(function_call.name);
    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
    std::vector<StackType> arguments;
    std::vector<Environment::DefineScope::Type> types;
    for (const auto& arg : function_call.args) {
        ExprCodeGen expr(jit);
        expr(arg);
        arguments.push_back(expr.get());
        types.push_back(arguments.back().second);
    }
    environment->beginDefineScope();
    builder.CreateCall(module.getFunction("beginScope"), { typed_environment_ptr });
    for (auto arg = arguments.begin(); const auto& parameter : cache_function.parameters) {
        Constant *name = ConstantDataArray::getString(module.getContext(), parameter.get().c_str());
        AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
        builder.CreateStore(name, str);
        auto result = *arg++;
        Environment::DefineScope::Type type;
        if (std::holds_alternative<AbaciValue::Type>(result.second)) {
            type = static_cast<AbaciValue::Type>(std::get<AbaciValue::Type>(result.second) | AbaciValue::Constant); 
        }
        else {
            type = result.second;
        }
        environment->getCurrentDefineScope()->setType(parameter.get(), type);
        auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
        builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
        builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), environmentTypeToType(type)), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
        Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
        Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
        builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), abaci_value, ConstantInt::get(builder.getInt1Ty(), true) });
    }
    auto type = jit.getCache()->getFunctionInstantiationType(function_call.name, types);
    environment->getCurrentDefineScope()->setType(RETURN_VAR, type);
    Constant *name = ConstantDataArray::getString(module.getContext(), RETURN_VAR);
    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
    builder.CreateStore(name, str);
    auto return_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
    builder.CreateStore(ConstantInt::get(builder.getInt64Ty(), 0), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), return_value, 0));
    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), environmentTypeToType(type)), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), return_value, 1));
    builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), return_value, ConstantInt::get(builder.getInt1Ty(), true) });
    std::string function_name{ mangled(function_call.name, types) };
    builder.CreateCall(module.getFunction(function_name), {});
    builder.CreateCall(module.getFunction("endScope"), { typed_environment_ptr });
    environment->endDefineScope();
}

template<>
void StmtCodeGen::codeGen(const ReturnStmt& return_stmt) const {
    ExprCodeGen expr(jit);
    expr(return_stmt.expression);
    auto result = expr.get();
    Constant *name = ConstantDataArray::getString(module.getContext(), RETURN_VAR);
    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
    builder.CreateStore(name, str);
    auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
    builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), environmentTypeToType(result.second)), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
    builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), abaci_value, ConstantInt::get(builder.getInt1Ty(), false) });
    for (int i = depth; i < (return_stmt.depth - 1); ++i) {
        builder.CreateCall(module.getFunction("endScope"), { typed_environment_ptr });
    }
    builder.CreateBr(exit_block);
}

template<>
void StmtCodeGen::codeGen([[maybe_unused]] const ExprFunction& expression_function) const {
}

template<>
void StmtCodeGen::codeGen([[maybe_unused]] const Class& class_template) const {
}

template<>
void StmtCodeGen::codeGen(const DataAssignStmt& data_assign) const {
    if (!environment->getCurrentDefineScope()->isDefined(data_assign.name.get())) {
        UnexpectedError1(VarNotExist, data_assign.name.get());
    }
    Constant *name = ConstantDataArray::getString(module.getContext(), data_assign.name.get().c_str());
    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
    builder.CreateStore(name, str);
    std::vector<int> indices;
    auto type = environment->getCurrentDefineScope()->getType(data_assign.name.get());
    for (const auto& member : data_assign.member_list) {
        if (!std::holds_alternative<Environment::DefineScope::Object>(type)) {
            UnexpectedError0(BadObject);
        }
        auto object = std::get<Environment::DefineScope::Object>(type);
        indices.push_back(jit.getCache()->getMemberIndex(jit.getCache()->getClass(object.class_name), member));
        type = object.object_types.at(indices.back());
    }
    indices.push_back(-1);
    ArrayType *array_type = ArrayType::get(builder.getInt32Ty(), indices.size());
    Value *indices_array = builder.CreateAlloca(array_type);
    for (int i = 0; auto idx : indices) {
        Value *element_ptr = builder.CreateGEP(array_type, indices_array, { builder.getInt32(0), builder.getInt32(i) });
        builder.CreateStore(builder.getInt32(idx), element_ptr);
        ++i;
    }
    Value *indices_ptr = builder.CreateGEP(array_type, indices_array, { builder.getInt32(0), builder.getInt32(0) });
    ExprCodeGen expr(jit);
    expr(data_assign.value);
    auto result = expr.get();
    if (!(type == result.second)) {
        UnexpectedError0(DataType);
    }
    auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
    builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), environmentTypeToType(result.second)), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
    builder.CreateCall(module.getFunction("setObjectData"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), indices_ptr, abaci_value });
}

template<>
void StmtCodeGen::codeGen(const MethodCall& method_call) const {
    Constant *object_name = ConstantDataArray::getString(module.getContext(), method_call.name.get().c_str());
    AllocaInst *object_str = builder.CreateAlloca(object_name->getType(), nullptr);
    builder.CreateStore(object_name, object_str);
    std::vector<int> indices;
    auto object_type = environment->getCurrentDefineScope()->getType(method_call.name.get());
    for (const auto& member : method_call.member_list) {
        if (!std::holds_alternative<Environment::DefineScope::Object>(object_type)) {
            UnexpectedError0(BadObject);
        }
        auto object = std::get<Environment::DefineScope::Object>(object_type);
        indices.push_back(jit.getCache()->getMemberIndex(jit.getCache()->getClass(object.class_name), member));
        object_type = object.object_types.at(indices.back());
    }
    indices.push_back(-1);
    ArrayType *array_type = ArrayType::get(builder.getInt32Ty(), indices.size());
    Value *indices_array = builder.CreateAlloca(array_type);
    for (int i = 0; auto idx : indices) {
        Value *element_ptr = builder.CreateGEP(array_type, indices_array, { builder.getInt32(0), builder.getInt32(i) });
        builder.CreateStore(builder.getInt32(idx), element_ptr);
        ++i;
    }
    Value *indices_ptr = builder.CreateGEP(array_type, indices_array, { builder.getInt32(0), builder.getInt32(0) });
    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
    auto data_value = builder.CreateCall(module.getFunction("getObjectData"), { typed_environment_ptr, builder.CreateBitCast(object_str, builder.getInt8PtrTy()), indices_ptr });
    builder.CreateCall(module.getFunction("setThisPtr"), { typed_environment_ptr, data_value });
    std::string function_name = std::get<Environment::DefineScope::Object>(object_type).class_name + '.' + method_call.method;
    const auto& cache_function = jit.getCache()->getFunction(function_name);
    std::vector<StackType> arguments;
    std::vector<Environment::DefineScope::Type> types;
    for (const auto& arg : method_call.args) {
        ExprCodeGen expr(jit);
        expr(arg);
        arguments.push_back(expr.get());
        types.push_back(arguments.back().second);
    }
    environment->beginDefineScope();
    builder.CreateCall(module.getFunction("beginScope"), { typed_environment_ptr });
    for (auto arg = arguments.begin(); const auto& parameter : cache_function.parameters) {
        Constant *name = ConstantDataArray::getString(module.getContext(), parameter.get().c_str());
        AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
        builder.CreateStore(name, str);
        auto result = *arg++;
        Environment::DefineScope::Type type;
        if (std::holds_alternative<AbaciValue::Type>(result.second)) {
            type = static_cast<AbaciValue::Type>(std::get<AbaciValue::Type>(result.second) | AbaciValue::Constant); 
        }
        else {
            type = result.second;
        }
        environment->getCurrentDefineScope()->setType(parameter.get(), type);
        auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
        builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
        builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), environmentTypeToType(type)), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
        builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), abaci_value, ConstantInt::get(builder.getInt1Ty(), true) });
    }
    auto type = jit.getCache()->getFunctionInstantiationType(function_name, types);
    environment->getCurrentDefineScope()->setType(RETURN_VAR, type);
    Constant *name = ConstantDataArray::getString(module.getContext(), RETURN_VAR);
    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
    builder.CreateStore(name, str);
    auto return_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
    builder.CreateStore(ConstantInt::get(builder.getInt64Ty(), 0), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), return_value, 0));
    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), environmentTypeToType(type)), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), return_value, 1));
    builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), return_value, ConstantInt::get(builder.getInt1Ty(), true) });
    builder.CreateCall(module.getFunction(mangled(function_name, types)), {});
    builder.CreateCall(module.getFunction("endScope"), { typed_environment_ptr });
    builder.CreateCall(module.getFunction("unsetThisPtr"), { typed_environment_ptr });
    environment->endDefineScope();
}

template<>
void StmtCodeGen::codeGen([[maybe_unused]] const ExpressionStmt& expression_stmt) const {
}

void StmtCodeGen::operator()(const StmtNode& stmt) const {
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
