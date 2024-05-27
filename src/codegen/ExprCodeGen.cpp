#include "CodeGen.hpp"
#include "engine/JIT.hpp"
#include "utility/Utility.hpp"
#include "parser/Messages.hpp"
#include <llvm/IR/Constants.h>
#include <algorithm>
#include <cstring>

using namespace llvm;

namespace abaci::codegen {

using abaci::ast::ExprNode;
using abaci::ast::ExprList;
using abaci::ast::ValueCall;
using abaci::ast::DataCall;
using abaci::ast::UserInput;
using abaci::ast::TypeConv;
using abaci::ast::MethodValueCall;
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
                    UnexpectedError0(NoAssignObject);
                default:
                    UnexpectedError0(BadType);
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
            auto type = environment->getCurrentDefineScope()->getType(variable.get());
            Value *value;
            switch (environmentTypeToType(type)) {
                case AbaciValue::Nil:
                case AbaciValue::Integer:
                    value = raw_value;
                    break;
                case AbaciValue::Boolean:
                    value = builder.CreateICmpNE(raw_value, ConstantInt::get(builder.getInt64Ty(), 0));
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
                case AbaciValue::Object:
                    value = builder.CreateBitCast(raw_value, PointerType::get(jit.getNamedType("struct.Object"), 0));
                    break;
                default:
                    UnexpectedError0(BadType);
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
                    std::vector<Environment::DefineScope::Type> types;
                    for (const auto& arg : call.args) {
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
                    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), environmentTypeToType(type)), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), return_value, 1));
                    builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), return_value, ConstantInt::get(builder.getInt1Ty(), true) });
                    std::string function_name{ mangled(call.name, types) };
                    builder.CreateCall(module.getFunction(function_name), {});
                    auto abaci_value = builder.CreateCall(module.getFunction("getVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()) });
                    Value *raw_value = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
                    Value *value;
                    switch (environmentTypeToType(type)) {
                        case AbaciValue::Nil:
                        case AbaciValue::Integer:
                            value = raw_value;
                            break;
                        case AbaciValue::Boolean:
                            value = builder.CreateICmpNE(raw_value, ConstantInt::get(builder.getInt64Ty(), 0));
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
                            UnexpectedError0(BadReturnType);
                    }
                    push({ value, type });
                    builder.CreateCall(module.getFunction("endScope"), { typed_environment_ptr });
                    environment->endDefineScope();
                    break;
                }
                case Cache::CacheClass: {
                    ArrayType *array = ArrayType::get(jit.getNamedType("struct.AbaciValue"), call.args.size());
                    AllocaInst *array_value = builder.CreateAlloca(array, nullptr);
                    Environment::DefineScope::Object stack_type;
                    stack_type.class_name = call.name;
                    for (int idx = 0; const auto& value : call.args) {
                        ExprCodeGen expr(jit);
                        expr(value);
                        auto elem_expr = expr.get();
                        Value *elem_ptr = builder.CreateGEP(array, array_value, { builder.getInt32(0), builder.getInt32(idx) });
                        Value *elem_value_ptr = builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), elem_ptr, 0);
                        Value *elem_type_ptr = builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), elem_ptr, 1);
                        builder.CreateStore(elem_expr.first, elem_value_ptr);
                        builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), environmentTypeToType(elem_expr.second)), elem_type_ptr);
                        stack_type.object_types.push_back(elem_expr.second);
                        ++idx;
                    }
                    Constant *name = ConstantDataArray::getString(module.getContext(), call.name.c_str());
                    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
                    builder.CreateStore(name, str);
                    Value *variables_sz = ConstantInt::get(builder.getInt64Ty(), call.args.size());
                    AllocaInst *stack_value = builder.CreateAlloca(jit.getNamedType("struct.Object"), nullptr);
                    builder.CreateStore(str, builder.CreateStructGEP(jit.getNamedType("struct.Object"), stack_value, 0));
                    builder.CreateStore(variables_sz, builder.CreateStructGEP(jit.getNamedType("struct.Object"), stack_value, 1));
                    builder.CreateStore(array_value, builder.CreateStructGEP(jit.getNamedType("struct.Object"), stack_value, 2));
                    push({ stack_value, stack_type });
                    break;
                }
                default:
                    UnexpectedError1(CallableNotExist, call.name);
            }
            break;
        }
        case ExprNode::DataNode: {
            const auto& data = std::get<DataCall>(node.get());
            Constant *name = ConstantDataArray::getString(module.getContext(), data.name.get().c_str());
            AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
            builder.CreateStore(name, str);
            std::vector<int> indices;
            auto type = environment->getCurrentDefineScope()->getType(data.name.get());
            for (const auto& member : data.member_list) {
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
            Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
            Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
            auto data_value = builder.CreateCall(module.getFunction("getObjectData"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), indices_ptr });
            Value *raw_value = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), data_value, 0));
            Value *value;
            switch (environmentTypeToType(type)) {
                case AbaciValue::Nil:
                case AbaciValue::Integer:
                    value = raw_value;
                    break;
                case AbaciValue::Boolean:
                    value = builder.CreateICmpNE(raw_value, ConstantInt::get(builder.getInt64Ty(), 0));
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
                case AbaciValue::Object:
                    value = builder.CreateBitCast(raw_value, PointerType::get(jit.getNamedType("struct.Object"), 0));
                    break;
                default:
                    UnexpectedError0(BadType);
            }
            push({ value, type });
            break;
        }
        case ExprNode::MethodNode: {
            const auto& method_call = std::get<MethodValueCall>(node.get());
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
            environment->getCurrentDefineScope()->setType("_return", type);
            Constant *name = ConstantDataArray::getString(module.getContext(), "_return");
            AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
            builder.CreateStore(name, str);
            auto return_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
            builder.CreateStore(ConstantInt::get(builder.getInt64Ty(), 0), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), return_value, 0));
            builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), environmentTypeToType(type)), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), return_value, 1));
            builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), return_value, ConstantInt::get(builder.getInt1Ty(), true) });
            builder.CreateCall(module.getFunction(mangled(function_name, types)), {});
            auto abaci_value = builder.CreateCall(module.getFunction("getVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()) });
            Value *raw_value = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
            Value *value;
            switch (environmentTypeToType(type)) {
                case AbaciValue::Nil:
                case AbaciValue::Integer:
                    value = raw_value;
                    break;
                case AbaciValue::Boolean:
                    value = builder.CreateICmpNE(raw_value, ConstantInt::get(builder.getInt64Ty(), 0));
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
                    UnexpectedError0(BadReturnType);
            }
            push({ value, type });
            builder.CreateCall(module.getFunction("endScope"), { typed_environment_ptr });
            builder.CreateCall(module.getFunction("unsetThisPtr"), { typed_environment_ptr });
            environment->endDefineScope();
            break;
        }
        case ExprNode::InputNode: {
            ArrayType *str = ArrayType::get(builder.getInt8Ty(), UserInput::MAX_SIZE);
            AllocaInst *ptr = builder.CreateAlloca(str, nullptr);
            auto string_value = builder.CreateAlloca(jit.getNamedType("struct.String"));
            auto len = ConstantInt::get(builder.getInt64Ty(), UserInput::MAX_SIZE);
            builder.CreateStore(ptr, builder.CreateStructGEP(jit.getNamedType("struct.String"), string_value, 0));
            builder.CreateStore(len, builder.CreateStructGEP(jit.getNamedType("struct.String"), string_value, 1));
            builder.CreateCall(module.getFunction("getUserInput"), { string_value });
            push({ string_value, AbaciValue::String });
            break;
        }
        case ExprNode::ConvNode: {
            const TypeConv& conversion = std::get<TypeConv>(node.get());
            ExprCodeGen expr(jit);
            expr(*(conversion.expression));
            auto convert = expr.get();
            auto convert_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
            builder.CreateStore(convert.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), convert_value, 0));
            builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), environmentTypeToType(convert.second)), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), convert_value, 1));
            auto type = conversion.to_type;
            auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
            builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), type), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
            switch (type) {
                case AbaciValue::Complex:
                    builder.CreateStore(builder.CreateAlloca(jit.getNamedType("struct.Complex")), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
                    break;
                case AbaciValue::String: {
                    Value *len;
                    if (environmentTypeToType(convert.second) == AbaciValue::String) {
                        Value *str = builder.CreateBitCast(convert.first, PointerType::get(jit.getNamedType("struct.String"), 0));
                        len = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.String"), str, 1));
                    }
                    else {
                        len = ConstantInt::get(builder.getInt64Ty(), TypeConv::MAX_SIZE);
                    }
                    AllocaInst *ptr = builder.CreateAlloca(builder.getInt8Ty(), len);
                    auto string_value = builder.CreateAlloca(jit.getNamedType("struct.String"));
                    builder.CreateStore(ptr, builder.CreateStructGEP(jit.getNamedType("struct.String"), string_value, 0));
                    builder.CreateStore(len, builder.CreateStructGEP(jit.getNamedType("struct.String"), string_value, 1));
                    builder.CreateStore(string_value, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
                    break;
                }
                default:
                    builder.CreateStore(ConstantInt::get(builder.getInt64Ty(), 0), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
                    break;
            }
            builder.CreateCall(module.getFunction("convertType"), { abaci_value, convert_value });
            Value *raw_value = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
            Value *value;
            switch (type) {
                case AbaciValue::Integer:
                    value = raw_value;
                    break;
                case AbaciValue::Real:
                case AbaciValue::Imaginary:
                    type = AbaciValue::Float;
                    [[fallthrough]];
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
                    UnexpectedError0(BadType);
            }
            push({ value, type });
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
                        auto type = !(operand.second == result.second) ? promote(result, operand) : result.second;
                        switch (environmentTypeToType(type)) {
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
                                        LogicError0(BadOperator);
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
                                        LogicError0(BadOperator);
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
                                        LogicError0(BadOperator);
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
                                        LogicError0(BadOperator);
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
                                        LogicError0(BadOperator);
                                }
                                break;
                            default:
                                LogicError0(BadType);
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
                        auto type = !(operand.second == result.second) ? promote(result, operand) : result.second;
                        switch (environmentTypeToType(type)) {
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
                                        LogicError0(BadOperator);
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
                                        LogicError0(BadOperator);
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
                                        LogicError0(BadOperator);
                                }
                                break;
                        default:
                            LogicError0(BadType);
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
                        switch (environmentTypeToType(type)) {
                            case AbaciValue::Boolean:
                                switch (op) {
                                    case Operator::Not:
                                    case Operator::Compl:
                                        result.first = builder.CreateNot(result.first);
                                        break;
                                    default:
                                        LogicError0(BadOperator);
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
                                        LogicError0(BadOperator);
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
                                        LogicError0(BadOperator);
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
                                        LogicError0(BadOperator);
                                }
                                break;
                        default:
                            LogicError0(BadType);
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
                            auto type = !(operand.second == result.second) ? promote(result, operand) : result.second;
                            switch (environmentTypeToType(type)) {
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
                                            LogicError0(BadOperator);
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
                                            LogicError0(BadOperator);
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
                                            LogicError0(BadOperator);
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
                                            LogicError0(BadOperator);
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
                                            LogicError0(BadOperator);
                                    }
                                    break;
                                }
                                default:
                                    LogicError0(BadType);
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
                    UnexpectedError0(BadAssociation);
            }
            break;
        }
        default:
            UnexpectedError0(BadNode);
    }
}

AbaciValue::Type ExprCodeGen::promote(StackType& a, StackType& b) const {
    if (std::holds_alternative<Environment::DefineScope::Object>(a.second)
        || std::holds_alternative<Environment::DefineScope::Object>(b.second)) {
        UnexpectedError0(NoObject);
    }
    if (a.second == b.second) {
        return environmentTypeToType(a.second);
    }
    auto type = std::max(environmentTypeToType(a.second), environmentTypeToType(b.second));
    switch (type) {
        case AbaciValue::Boolean:
        case AbaciValue::Integer:
            break;
        case AbaciValue::Float:
            switch (environmentTypeToType(a.second)) {
                case AbaciValue::Integer:
                    a.first = builder.CreateSIToFP(a.first, Type::getDoubleTy(module.getContext()));
                    break;
                default:
                    break;
            }
            switch (environmentTypeToType(b.second)) {
                case AbaciValue::Integer:
                    b.first = builder.CreateSIToFP(b.first, Type::getDoubleTy(module.getContext()));
                    break;
                default:
                    break;
            }
            break;
        case AbaciValue::Complex: {
            switch (environmentTypeToType(a.second)) {
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
            switch (environmentTypeToType(b.second)) {
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
            UnexpectedError0(BadCoerceTypes);
    }
    a.second = b.second = type;
    return type;
}

Value *ExprCodeGen::toBoolean(StackType& v) const {
    Value *boolean;
    switch (environmentTypeToType(v.second)) {
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
            UnexpectedError0(NoBoolean);
    }
    return boolean;
}

} // namespace abaci::codegen
