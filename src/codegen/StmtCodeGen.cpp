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
                builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), result.second), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
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
                        UnexpectedError("Bad print operator.");
                }
                break;
            }
            default:
                UnexpectedError("Bad field.");
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
    auto type = static_cast<AbaciValue::Type>(result.second | ((define.assign == Operator::Equal) ? AbaciValue::Constant : 0));
    if (environment->getCurrentDefineScope()->getEnclosing()) {
        environment->getCurrentDefineScope()->setType(define.name.get(), type);
    }
    auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
    builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), type), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
    builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), abaci_value, ConstantInt::get(builder.getInt1Ty(), true) });
}

template<>
void StmtCodeGen::codeGen(const AssignStmt& assign) const {
    if (!environment->getCurrentDefineScope()->isDefined(assign.name.get())) {
        LogicError("No such variable.");
    }
    else if (environment->getCurrentDefineScope()->getType(assign.name.get()) & AbaciValue::Constant) {
        LogicError("Cannot assign to constant.");
    }
    Constant *name = ConstantDataArray::getString(module.getContext(), assign.name.get().c_str());
    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
    builder.CreateStore(name, str);
    ExprCodeGen expr(jit);
    expr(assign.value);
    auto result = expr.get();
    if (environment->getCurrentDefineScope()->getType(assign.name.get()) != result.second) {
        LogicError("Cannot assign to variable with different type.");
    }
    auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
    builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), result.second), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
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
    if (result.second == AbaciValue::Boolean) {
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
    if (result.second == AbaciValue::Boolean) {
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
    if (result.second == AbaciValue::Boolean) {
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
                UnexpectedError("Not yet implemented.");
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
    std::vector<AbaciValue::Type> types;
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
        auto type = static_cast<AbaciValue::Type>(result.second | AbaciValue::Constant);
        environment->getCurrentDefineScope()->setType(parameter.get(), type);
        auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
        builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
        builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), type), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
        Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
        Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
        builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), abaci_value, ConstantInt::get(builder.getInt1Ty(), true) });
    }
    auto type = jit.getCache()->getFunctionInstantiationType(function_call.name, types);
    environment->getCurrentDefineScope()->setType("_return", type);
    Constant *name = ConstantDataArray::getString(module.getContext(), "_return");
    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
    builder.CreateStore(name, str);
    auto return_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
    builder.CreateStore(ConstantInt::get(builder.getInt64Ty(), 0), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), return_value, 0));
    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), type), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), return_value, 1));
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
    Constant *name = ConstantDataArray::getString(module.getContext(), "_return");
    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
    builder.CreateStore(name, str);
    auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
    builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), result.second & AbaciValue::TypeMask), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
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
    else {
        UnexpectedError("Bad StmtNode type.");
    }
}

} // namespace abaci::codegen
