#include "CodeGen.hpp"
#include "engine/JIT.hpp"
#include "utility/Utility.hpp"
#include <llvm/IR/Constants.h>
#include <algorithm>
#include <cstring>

using namespace llvm;

namespace abaci::codegen {

using abaci::ast::ExprNode;
using abaci::ast::ExprList;
using abaci::ast::ValueCall;
using abaci::utility::Operator;

void ExprCodeGen::operator()(const abaci::ast::ExprNode& node) const {
    switch (node.get().index()) {
        case ExprNode::ValueNode: {
            auto value = std::get<AbaciValue>(node.get());
            switch (value.type) {
                case AbaciValue::Nil:
                    push({ ConstantInt::get(builder.getInt8Ty(), 0), value.type });
                    break;
                case AbaciValue::Boolean:
                    push({ ConstantInt::get(builder.getInt1Ty(), value.value.boolean), value.type });
                    break;
                case AbaciValue::Integer:
                    push({ ConstantInt::get(builder.getInt64Ty(), value.value.integer), value.type });
                    break;
                case AbaciValue::Float:
                    push({ ConstantFP::get(builder.getDoubleTy(), value.value.floating), value.type });
                    break;
                case AbaciValue::Complex: {
                    auto complex_value = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                    auto real = ConstantFP::get(builder.getDoubleTy(), value.value.complex->real);
                    auto imag = ConstantFP::get(builder.getDoubleTy(), value.value.complex->imag);
                    auto complex_constant = ConstantStruct::get(jit.getNamedType("struct.Complex"), { real, imag });
                    builder.CreateStore(complex_constant, complex_value);
                    push({ complex_value, value.type });
                    break;
                }
                case AbaciValue::String: {
                    Constant *str = ConstantDataArray::getString(module.getContext(),
                        reinterpret_cast<char*>(value.value.str->ptr));
                    AllocaInst *ptr = builder.CreateAlloca(str->getType(), nullptr);
                    builder.CreateStore(str, ptr);
                    auto string_value = builder.CreateAlloca(jit.getNamedType("struct.String"));
                    auto len = ConstantInt::get(builder.getInt64Ty(), value.value.str->len);
                    builder.CreateStore(ptr, builder.CreateStructGEP(jit.getNamedType("struct.String"), string_value, 0));
                    builder.CreateStore(len, builder.CreateStructGEP(jit.getNamedType("struct.String"), string_value, 1));
                    push({ string_value, value.type });
                    break;
                }
                case AbaciValue::Object:
                    UnexpectedError("Cannot reassign objects.");
                default:
                    UnexpectedError("Not yet implemented");
            }
            break;
        }
        case ExprNode::VariableNode: {
            auto variable = std::get<Variable>(node.get());
            Constant *name = ConstantDataArray::getString(module.getContext(), variable.get().c_str());
            AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
            builder.CreateStore(name, str);
            Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
            Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
            auto abaci_value = builder.CreateCall(module.getFunction("getVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()) });
            Value *raw_value = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
            auto type = static_cast<AbaciValue::Type>(environment->getCurrentDefineScope()->getType(variable.get()) & AbaciValue::TypeMask);
            Value *value;
            switch (type) {
                case AbaciValue::Nil:
                case AbaciValue::Integer:
                    value = raw_value;
                    break;
                case AbaciValue::Boolean:
                    value = builder.CreateBitCast(raw_value, builder.getInt1Ty());
                    break;
                case AbaciValue::Float:
                    value = builder.CreateBitCast(raw_value, builder.getDoubleTy());
                    break;
                case AbaciValue::Complex:
                    value = builder.CreateBitCast(raw_value, PointerType::get(jit.getNamedType("struct.Complex"), 0));
                    break;
                case AbaciValue::String:
                    value = builder.CreateBitCast(raw_value, PointerType::get(jit.getNamedType("struct.String"), 0));
                    break;
                default:
                    UnexpectedError("Bad type for dereference");
            }
            push({ value, type });
            break;
        }
        case ExprNode::CallNode: {
            const auto& call = std::get<ValueCall>(node.get());
            switch(jit.getCache()->getCacheType(call.name)) {
                case Cache::CacheFunction: {
                    const auto& cache_function = jit.getCache()->getFunction(call.name);
                    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(jit.getEnvironment()));
                    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
                    std::vector<StackType> arguments;
                    std::vector<AbaciValue::Type> types;
                    for (const auto& arg : call.args) {
                        ExprCodeGen expr(jit);
                        expr(arg);
                        arguments.push_back(expr.get());
                        types.push_back(static_cast<AbaciValue::Type>(arguments.back().second & AbaciValue::TypeMask));
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
                        Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(jit.getEnvironment()));
                        Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
                        builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), abaci_value, ConstantInt::get(builder.getInt1Ty(), true) });
                    }
                    auto type = jit.getCache()->getFunctionInstantiationType(call.name, types);
                    environment->getCurrentDefineScope()->setType("_return", type);
                    Constant *name = ConstantDataArray::getString(module.getContext(), "_return");
                    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
                    builder.CreateStore(name, str);
                    auto return_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
                    builder.CreateStore(ConstantInt::get(builder.getInt64Ty(), 0), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), return_value, 0));
                    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), type), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), return_value, 1));
                    builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), return_value, ConstantInt::get(builder.getInt1Ty(), true) });
                    std::string function_name{ mangled(call.name, types) };
                    builder.CreateCall(module.getFunction(function_name), {});
                    auto abaci_value = builder.CreateCall(module.getFunction("getVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()) });
                    Value *raw_value = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
                    Value *value;
                    switch (type) {
                        case AbaciValue::Nil:
                        case AbaciValue::Integer:
                            value = raw_value;
                            break;
                        case AbaciValue::Boolean:
                            value = builder.CreateBitCast(raw_value, builder.getInt1Ty());
                            break;
                        case AbaciValue::Float:
                            value = builder.CreateBitCast(raw_value, builder.getDoubleTy());
                            break;
                        case AbaciValue::Complex: {
                            raw_value = builder.CreateBitCast(raw_value, PointerType::get(jit.getNamedType("struct.Complex"), 0));
                            value = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                            Value *real = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), raw_value, 0));
                            Value *imag = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), raw_value, 1));
                            builder.CreateStore(real, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), value, 0));
                            builder.CreateStore(imag, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), value, 1));
                            break;
                        }
                        case AbaciValue::String: {
                            raw_value = builder.CreateBitCast(raw_value, PointerType::get(jit.getNamedType("struct.String"), 0));
                            value = builder.CreateAlloca(jit.getNamedType("struct.String"));
                            Value *str = builder.CreateLoad(builder.getInt8PtrTy(), builder.CreateStructGEP(jit.getNamedType("struct.String"), raw_value, 0));
                            Value *len = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.String"), raw_value, 1));
                            AllocaInst *ptr = builder.CreateAlloca(builder.getInt8Ty(), len);
                            builder.CreateMemCpy(ptr, MaybeAlign(1), str, MaybeAlign(1), len);
                            builder.CreateStore(ptr, builder.CreateStructGEP(jit.getNamedType("struct.String"), value, 0));
                            builder.CreateStore(len, builder.CreateStructGEP(jit.getNamedType("struct.String"), value, 1));
                            break;
                        }
                        default:
                            UnexpectedError("Bad type for dereference");
                    }
                    push({ value, type });
                    builder.CreateCall(module.getFunction("endScope"), { typed_environment_ptr });
                    environment->endDefineScope();
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
                    auto result = pop();
                    for (auto iter = ++expr.begin(); iter != expr.end();) {
                        auto op = std::get<Operator>(iter++->get());
                        (*this)(*iter++);
                        auto operand = pop();
                        auto type = (operand.second != result.second) ? promote(result, operand) : result.second;
                        switch (type) {
                            case AbaciValue::Boolean:
                                switch (op) {
                                    case Operator::BitAnd:
                                        result.first = builder.CreateAnd(result.first, operand.first);
                                        break;
                                    case Operator::BitXor:
                                        result.first = builder.CreateXor(result.first, operand.first);
                                        break;
                                    case Operator::BitOr:
                                        result.first = builder.CreateOr(result.first, operand.first);
                                        break;
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Integer:
                                switch (op) {
                                    case Operator::Plus:
                                        result.first = builder.CreateAdd(result.first, operand.first);
                                        break;
                                    case Operator::Minus:
                                        result.first = builder.CreateSub(result.first, operand.first);
                                        break;
                                    case Operator::Times:
                                        result.first = builder.CreateMul(result.first, operand.first);
                                        break;
                                    case Operator::Modulo:
                                        result.first = builder.CreateSRem(result.first, operand.first);
                                        break;
                                    case Operator::FloorDivide:
                                        result.first = builder.CreateSDiv(result.first, operand.first);
                                        break;
                                    case Operator::Divide: {
                                        auto a = builder.CreateSIToFP(result.first, Type::getDoubleTy(module.getContext()));
                                        auto b = builder.CreateSIToFP(operand.first, Type::getDoubleTy(module.getContext()));
                                        result.first = builder.CreateFDiv(a, b);
                                        result.second = AbaciValue::Float;
                                        break;
                                    }
                                    case Operator::BitAnd:
                                        result.first = builder.CreateAnd(result.first, operand.first);
                                        break;
                                    case Operator::BitXor:
                                        result.first = builder.CreateXor(result.first, operand.first);
                                        break;
                                    case Operator::BitOr:
                                        result.first = builder.CreateOr(result.first, operand.first);
                                        break;
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Float:
                                switch (op) {
                                    case Operator::Plus:
                                        result.first = builder.CreateFAdd(result.first, operand.first);
                                        break;
                                    case Operator::Minus:
                                        result.first = builder.CreateFSub(result.first, operand.first);
                                        break;
                                    case Operator::Times:
                                        result.first = builder.CreateFMul(result.first, operand.first);
                                        break;
                                    case Operator::Divide:
                                        result.first = builder.CreateFDiv(result.first, operand.first);
                                        break;
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Complex:
                                switch (op) {
                                    case Operator::Plus:
                                    case Operator::Minus:
                                    case Operator::Times:
                                    case Operator::Divide: {
                                        Function *complexMathFunc = module.getFunction("complexMath");
                                        auto cplx = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                                        builder.CreateCall(complexMathFunc,
                                            { cplx, ConstantInt::get(builder.getInt32Ty(), static_cast<int>(op)),
                                                result.first, operand.first });
                                        result.first = cplx;
                                        break;
                                    }
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::String:
                                switch (op) {
                                    case Operator::Plus: {
                                        auto str = builder.CreateAlloca(jit.getNamedType("struct.String"));
                                        Value *str1_len = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.String"), result.first, 1));
                                        Value *str2_len = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.String"), operand.first, 1));
                                        Value *total_len = builder.CreateAdd(builder.CreateAdd(str1_len, str2_len), ConstantInt::get(builder.getInt64Ty(), 1));
                                        AllocaInst *ptr = builder.CreateAlloca(builder.getInt8Ty(), total_len);
                                        Value *str1_ptr = builder.CreateLoad(builder.getInt8PtrTy(), builder.CreateStructGEP(jit.getNamedType("struct.String"), result.first, 0));
                                        Value *str2_ptr = builder.CreateLoad(builder.getInt8PtrTy(), builder.CreateStructGEP(jit.getNamedType("struct.String"), operand.first, 0));
                                        builder.CreateMemCpy(ptr, MaybeAlign(1), str1_ptr, MaybeAlign(1), str1_len);
                                        Value *ptr_concat = builder.CreateGEP(builder.getInt8Ty(), ptr, str1_len);
                                        builder.CreateMemCpy(ptr_concat, MaybeAlign(1), str2_ptr, MaybeAlign(1), str2_len);
                                        Value *ptr_concat_null = builder.CreateGEP(builder.getInt8Ty(), ptr_concat, str2_len);
                                        builder.CreateStore(ConstantInt::get(builder.getInt8Ty(), 0), ptr_concat_null);
                                        builder.CreateStore(ptr, builder.CreateStructGEP(jit.getNamedType("struct.String"), str, 0));
                                        builder.CreateStore(total_len, builder.CreateStructGEP(jit.getNamedType("struct.String"), str, 1));
                                        result.first = str;
                                        break;
                                    }
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            default:
                                UnexpectedError("Not yet implemented");
                                break;
                        }
                    }
                    push(result);
                    break;
                }
                case ExprNode::Right: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.back());
                    auto result = pop();
                    for (auto iter = ++expr.rbegin(); iter != expr.rend();) {
                        auto op = std::get<Operator>(iter++->get());
                        (*this)(*iter++);
                        auto operand = pop();
                        auto type = (operand.second != result.second) ? promote(result, operand) : result.second;
                        switch (type) {
                            case AbaciValue::Integer:
                                switch (op) {
                                    case Operator::Exponent: {
                                        auto a = builder.CreateSIToFP(result.first, Type::getDoubleTy(module.getContext()));
                                        auto b = builder.CreateSIToFP(operand.first, Type::getDoubleTy(module.getContext()));
                                        Function *powFunc = module.getFunction("pow");
                                        result.first = builder.CreateCall(powFunc, { b, a });
                                        result.second = AbaciValue::Float;
                                        break;
                                    }
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Float:
                                switch (op) {
                                    case Operator::Exponent: {
                                        Function *powFunc = module.getFunction("pow");
                                        result.first = builder.CreateCall(powFunc, { operand.first, result.first });
                                        break;
                                    }
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Complex:
                                switch (op) {
                                    case Operator::Exponent: {
                                        Function *complexMathFunc = module.getFunction("complexMath");
                                        auto cplx = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                                        builder.CreateCall(complexMathFunc,
                                            { cplx, ConstantInt::get(builder.getInt32Ty(), static_cast<int>(op)),
                                                operand.first, result.first });
                                        result.first = cplx;
                                        break;
                                    }
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                        default:
                            UnexpectedError("Not yet implemented");
                            break;
                        }
                    }
                    push(result);
                    break;
                }
                case ExprNode::Unary: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.back());
                    auto result = pop();
                    auto type = result.second;
                    for (auto iter = ++expr.rbegin(); iter != expr.rend();) {
                        auto op = std::get<Operator>(iter++->get());
                        switch (type) {
                            case AbaciValue::Boolean:
                                switch (op) {
                                    case Operator::Not:
                                    case Operator::Compl:
                                        result.first = builder.CreateNot(result.first);
                                        break;
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Integer:
                                switch (op) {
                                    case Operator::Minus:
                                        result.first = builder.CreateNeg(result.first);
                                        break;
                                    case Operator::Not:
                                        toBoolean(result);
                                        result.first = builder.CreateNot(toBoolean(result));
                                        result.second = AbaciValue::Boolean;
                                        break;
                                    case Operator::Compl:
                                        result.first = builder.CreateNot(result.first);
                                        break;
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Float:
                                switch (op) {
                                    case Operator::Minus:
                                        result.first = builder.CreateFNeg(result.first);
                                        break;
                                    case Operator::Not:
                                        result.first = builder.CreateNot(toBoolean(result));
                                        result.second = AbaciValue::Boolean;
                                        break;
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Complex:
                                switch (op) {
                                    case Operator::Minus: {
                                        Function *complexMathFunc = module.getFunction("complexMath");
                                        auto cplx = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                                        builder.CreateCall(complexMathFunc,
                                            { cplx, ConstantInt::get(builder.getInt32Ty(), static_cast<int>(op)),
                                                result.first, ConstantPointerNull::get(PointerType::get(jit.getNamedType("struct.Complex"), 0)) });
                                        result.first = cplx;
                                        break;
                                    }
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                        default:
                            UnexpectedError("Not yet implemented");
                            break;
                        }
                    }
                    push(result);
                    break;
                }
                case ExprNode::Boolean: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.front());
                    auto result = pop();
                    if (expr.size() == 1) {
                        push(result);
                    }
                    else {
                        Value *bool_result = ConstantInt::get(builder.getInt1Ty(), true);
                        for (auto iter = ++expr.begin(); iter != expr.end();) {
                            auto op = std::get<Operator>(iter++->get());
                            (*this)(*iter++);
                            auto operand = pop();
                            auto type = (operand.second != result.second) ? promote(result, operand) : result.second;
                            switch (type) {
                                case AbaciValue::Boolean:
                                    switch (op) {
                                        case Operator::Equal:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpEQ(result.first, operand.first));
                                            break;
                                        case Operator::NotEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpNE(result.first, operand.first));
                                            break;
                                        case Operator::Less:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpULT(result.first, operand.first));
                                            break;
                                        case Operator::LessEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpULE(result.first, operand.first));
                                            break;
                                        case Operator::GreaterEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpUGE(result.first, operand.first));
                                            break;
                                        case Operator::Greater:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpUGT(result.first, operand.first));
                                            break;
                                        case Operator::And:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateAnd(result.first, operand.first));
                                            break;
                                        case Operator::Or:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateOr(result.first, operand.first));
                                            break;
                                        default:
                                            UnexpectedError("Unknown operator.");
                                    }
                                    break;
                                case AbaciValue::Integer:
                                    switch (op) {
                                        case Operator::Equal:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpEQ(result.first, operand.first));
                                            break;
                                        case Operator::NotEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpNE(result.first, operand.first));
                                            break;
                                        case Operator::Less:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpSLT(result.first, operand.first));
                                            break;
                                        case Operator::LessEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpSLE(result.first, operand.first));
                                            break;
                                        case Operator::GreaterEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpSGE(result.first, operand.first));
                                            break;
                                        case Operator::Greater:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpSGT(result.first, operand.first));
                                            break;
                                        case Operator::And:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateAnd(toBoolean(result), toBoolean(operand)));
                                            break;
                                        case Operator::Or:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateOr(toBoolean(result), toBoolean(operand)));
                                            break;
                                        default:
                                            UnexpectedError("Unknown operator.");
                                    }
                                    break;
                                case AbaciValue::Float:
                                    switch (op) {
                                        case Operator::Equal:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateFCmpOEQ(result.first, operand.first));
                                            break;
                                        case Operator::NotEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateFCmpONE(result.first, operand.first));
                                            break;
                                        case Operator::Less:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateFCmpOLT(result.first, operand.first));
                                            break;
                                        case Operator::LessEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateFCmpOLE(result.first, operand.first));
                                            break;
                                        case Operator::GreaterEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateFCmpOGE(result.first, operand.first));
                                            break;
                                        case Operator::Greater:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateFCmpOGT(result.first, operand.first));
                                            break;
                                        case Operator::And:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateAnd(toBoolean(result), toBoolean(operand)));
                                            break;
                                        case Operator::Or:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateOr(toBoolean(result), toBoolean(operand)));
                                            break;
                                        default:
                                            UnexpectedError("Unknown operator.");
                                    }
                                    break;
                                case AbaciValue::Complex: {
                                    Value *real_value1 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), result.first, 0));
                                    Value *imag_value1 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), result.first, 1));
                                    Value *real_value2 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), operand.first, 0));
                                    Value *imag_value2 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), operand.first, 1));
                                    switch (op) {
                                        case Operator::Equal:
                                            bool_result = builder.CreateAnd(bool_result,
                                                builder.CreateAnd(builder.CreateFCmpOEQ(real_value1, real_value2), builder.CreateFCmpOEQ(imag_value1, imag_value2)));
                                            break;
                                        case Operator::NotEqual:
                                            bool_result = builder.CreateAnd(bool_result,
                                                builder.CreateOr(builder.CreateFCmpONE(real_value1, real_value2), builder.CreateFCmpONE(imag_value1, imag_value2)));
                                            break;
                                        default:
                                            UnexpectedError("Unknown operator.");
                                    }
                                    break;
                                }
                                case AbaciValue::String: {
                                    Value *str1_ptr = builder.CreateLoad(builder.getInt8PtrTy(), builder.CreateStructGEP(jit.getNamedType("struct.String"), result.first, 0));
                                    Value *str2_ptr = builder.CreateLoad(builder.getInt8PtrTy(), builder.CreateStructGEP(jit.getNamedType("struct.String"), operand.first, 0));
                                    switch (op) {
                                        case Operator::Equal:
                                            bool_result = builder.CreateAnd(bool_result,
                                                builder.CreateICmpEQ(ConstantInt::get(builder.getInt32Ty(), 0),
                                                builder.CreateCall(module.getFunction("strcmp"), { str1_ptr, str2_ptr })));
                                            break;
                                        case Operator::NotEqual:
                                            bool_result = builder.CreateAnd(bool_result,
                                                builder.CreateICmpNE(ConstantInt::get(builder.getInt32Ty(), 0),
                                                builder.CreateCall(module.getFunction("strcmp"), { str1_ptr, str2_ptr })));
                                            break;
                                        default:
                                            UnexpectedError("Unknown operator.");
                                    }
                                    break;
                                }
                                default:
                                    UnexpectedError("Not yet implemented");
                                    break;
                            }
                            result.first = operand.first;
                            result.second = type;
                        }
                        result.first = bool_result;
                        result.second = AbaciValue::Boolean;
                        push(result);
                    }
                    break;
                }
                default:
                    UnexpectedError("Unknown node association type.");
            }
            break;
        }
        default:
            UnexpectedError("Bad node type.");
    }
}

AbaciValue::Type ExprCodeGen::promote(StackType& a, StackType& b) const {
    if (a.second == b.second) {
        return a.second;
    }
    auto type = std::max(a.second, b.second);
    switch (type) {
        case AbaciValue::Boolean:
        case AbaciValue::Integer:
            break;
        case AbaciValue::Float:
            switch (a.second) {
                case AbaciValue::Integer:
                    a.first = builder.CreateSIToFP(a.first, Type::getDoubleTy(module.getContext()));
                    break;
                default:
                    break;
            }
            switch (b.second) {
                case AbaciValue::Integer:
                    b.first = builder.CreateSIToFP(b.first, Type::getDoubleTy(module.getContext()));
                    break;
                default:
                    break;
            }
            break;
        case AbaciValue::Complex: {
            switch (a.second) {
                case AbaciValue::Integer: {
                    auto real = builder.CreateSIToFP(a.first, Type::getDoubleTy(module.getContext()));
                    auto imag = ConstantFP::get(builder.getDoubleTy(), 0.0);
                    a.first = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                    builder.CreateStore(real, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), a.first, 0));
                    builder.CreateStore(imag, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), a.first, 1));
                    break;
                }
                case AbaciValue::Float: {
                    auto real = a.first;
                    auto imag = ConstantFP::get(builder.getDoubleTy(), 0.0);
                    a.first = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                    builder.CreateStore(real, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), a.first, 0));
                    builder.CreateStore(imag, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), a.first, 1));
                    break;
                }
                default:
                    break;
            }
            switch (b.second) {
                case AbaciValue::Integer: {
                    auto real = builder.CreateSIToFP(b.first, Type::getDoubleTy(module.getContext()));
                    auto imag = ConstantFP::get(builder.getDoubleTy(), 0.0);
                    b.first = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                    builder.CreateStore(real, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), b.first, 0));
                    builder.CreateStore(imag, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), b.first, 1));
                    break;
                }
                case AbaciValue::Float: {
                    auto real = b.first;
                    auto imag = ConstantFP::get(builder.getDoubleTy(), 0.0);
                    b.first = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                    builder.CreateStore(real, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), b.first, 0));
                    builder.CreateStore(imag, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), b.first, 1));
                    break;
                }
                default:
                    break;
            }
            break;
        }
        default:
            UnexpectedError("Not yet implemented.");
    }
    return a.second = b.second = type;
}

Value *ExprCodeGen::toBoolean(StackType& v) const {
    Value *boolean;
    switch (v.second) {
        case AbaciValue::Boolean:
            boolean = v.first;
            break;
        case AbaciValue::Integer:
            boolean = builder.CreateICmpNE(v.first, ConstantInt::get(builder.getInt64Ty(), 0));
            break;
        case AbaciValue::Float:
            boolean = builder.CreateFCmpONE(v.first, ConstantFP::get(builder.getDoubleTy(), 0.0));
            break;
        case AbaciValue::String: {
            Value *length = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.String"), v.first, 1));
            boolean = builder.CreateICmpNE(length, ConstantInt::get(builder.getInt64Ty(), 0));
            break;
        }
        default:
            UnexpectedError("Not yet implemented.");
    }
    return boolean;
}

}
