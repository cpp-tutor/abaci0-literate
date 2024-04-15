#include "JIT.hpp"
#include "codegen/CodeGen.hpp"
#include "lib/Abaci.hpp"
#include "utility/Report.hpp"
#include <llvm/IR/Verifier.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/IR/Type.h>
#include <stdexcept>
#include <memory>
#include <cmath>
#include <cstring>

using namespace llvm;
using namespace llvm::orc;

namespace abaci::engine {

using abaci::codegen::StmtCodeGen;
using abaci::ast::ReturnStmt; 

void JIT::initialize() {
    FunctionType *pow_func_type = FunctionType::get(builder.getDoubleTy(), { builder.getDoubleTy(), builder.getDoubleTy() }, false);
    Function::Create(pow_func_type, Function::ExternalLinkage, "pow", module.get());
    FunctionType *strcmp_func_type = FunctionType::get(builder.getInt32Ty(), { builder.getInt8PtrTy(), builder.getInt8PtrTy() }, false);
    Function::Create(strcmp_func_type, Function::ExternalLinkage, "strcmp", module.get());
    StructType *complex_type = StructType::create(*context, "struct.Complex");
    complex_type->setBody({ Type::getDoubleTy(*context), Type::getDoubleTy(*context) });
    StructType *string_type = StructType::create(*context, "struct.String");
    string_type->setBody({ Type::getInt8PtrTy(*context), Type::getInt64Ty(*context) });
    FunctionType *dummy_type = FunctionType::get(builder.getVoidTy(), { PointerType::get(string_type, 0) }, false);
    Function::Create(dummy_type, Function::ExternalLinkage, "stringFunc", module.get());
    auto union_type = Type::getInt64Ty(*context); 
    auto enum_type = Type::getInt32Ty(*context);
    FunctionType *complex_math_type = FunctionType::get(builder.getVoidTy(), { PointerType::get(complex_type, 0), builder.getInt32Ty(), PointerType::get(complex_type, 0), PointerType::get(complex_type, 0) }, false);
    Function::Create(complex_math_type, Function::ExternalLinkage, "complexMath", module.get());
    StructType *abaci_value_type = StructType::create(*context, "struct.AbaciValue");
    abaci_value_type->setBody({ union_type, enum_type });
    StructType *environment_type = StructType::create(*context, "struct.Environment");
    FunctionType *print_value_type = FunctionType::get(builder.getVoidTy(), { PointerType::get(abaci_value_type, 0) }, false);
    Function::Create(print_value_type, Function::ExternalLinkage, "printValue", module.get());
    FunctionType *print_comma_type = FunctionType::get(builder.getVoidTy(), {}, false);
    Function::Create(print_comma_type, Function::ExternalLinkage, "printComma", module.get());
    FunctionType *print_ln_type = FunctionType::get(builder.getVoidTy(), {}, false);
    Function::Create(print_ln_type, Function::ExternalLinkage, "printLn", module.get());
    FunctionType *set_variable_type = FunctionType::get(builder.getVoidTy(), { PointerType::get(environment_type, 0), builder.getInt8PtrTy(), PointerType::get(abaci_value_type, 0), builder.getInt1Ty() }, false);
    Function::Create(set_variable_type, Function::ExternalLinkage, "setVariable", module.get());
    FunctionType *get_variable_type = FunctionType::get(PointerType::get(abaci_value_type, 0), { PointerType::get(environment_type, 0), builder.getInt8PtrTy() }, false);
    Function::Create(get_variable_type, Function::ExternalLinkage, "getVariable", module.get());
    FunctionType *begin_scope_type = FunctionType::get(builder.getVoidTy(), { PointerType::get(environment_type, 0) }, false);
    Function::Create(begin_scope_type, Function::ExternalLinkage, "beginScope", module.get());
    FunctionType *end_scope_type = FunctionType::get(builder.getVoidTy(), { PointerType::get(environment_type, 0) }, false);
    Function::Create(end_scope_type, Function::ExternalLinkage, "endScope", module.get());
    for (const auto& instantiation : cache->getInstantiations()) {
        std::string function_name{ mangled(instantiation.name, instantiation.parameter_types) };
        FunctionType *inst_func_type = FunctionType::get(builder.getVoidTy(), {}, false);
        current_function = Function::Create(inst_func_type, Function::ExternalLinkage, function_name, *module);
        BasicBlock *entry_block = BasicBlock::Create(*context, "entry", current_function);
        BasicBlock *exit_block = BasicBlock::Create(*context, "exit", current_function);
        builder.SetInsertPoint(entry_block);
        const auto& cache_function = cache->getFunction(instantiation.name);
        auto current_scope = environment->getCurrentDefineScope();
        environment->setCurrentDefineScope(instantiation.scope);
        StmtCodeGen stmt(*this, exit_block, instantiation.scope->getDepth());
        Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
        Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(environment_type, 0));
        builder.CreateCall(module->getFunction("beginScope"), { typed_environment_ptr });
        for (const auto& function_stmt : cache_function.body) {
            stmt(function_stmt);
        }
        if (!dynamic_cast<const ReturnStmt*>(cache_function.body.back().get())) {
            builder.CreateBr(exit_block);
        }
        builder.SetInsertPoint(exit_block);
        builder.CreateCall(module->getFunction("endScope"), { typed_environment_ptr });
        builder.CreateRetVoid();
        environment->setCurrentDefineScope(current_scope);
    }
    FunctionType *main_func_type = FunctionType::get(builder.getVoidTy(), {}, false);
    current_function = Function::Create(main_func_type, Function::ExternalLinkage, function_name, module.get());
    BasicBlock *entry_block = BasicBlock::Create(*context, "entry", current_function);
    builder.SetInsertPoint(entry_block);
}

StmtFunctionType JIT::getExecFunction() {
    builder.CreateRetVoid();
    cache->clearInstantiations();
    //verifyModule(*module, &errs());
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    jit = jitBuilder.create();
    if (!jit) {
        UnexpectedError("Failed to create LLJIT instance");
    }
    if (auto err = (*jit)->addIRModule(ThreadSafeModule(std::move(module), std::move(context)))) {
        handleAllErrors(std::move(err), [&](ErrorInfoBase& eib) {
            errs() << "Error: " << eib.message() << '\n';
        });
        UnexpectedError("Failed add IR module");
    }
    if (auto err = (*jit)->getMainJITDylib().define(
            llvm::orc::absoluteSymbols({{(*jit)->getExecutionSession().intern("pow"), { reinterpret_cast<uintptr_t>(&pow), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("strcmp"), { reinterpret_cast<uintptr_t>(&strcmp), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("memcpy"), { reinterpret_cast<uintptr_t>(&memcpy), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("complexMath"), { reinterpret_cast<uintptr_t>(&complexMath), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("printValue"), { reinterpret_cast<uintptr_t>(&printValue), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("printComma"), { reinterpret_cast<uintptr_t>(&printComma), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("printLn"), { reinterpret_cast<uintptr_t>(&printLn), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("setVariable"), { reinterpret_cast<uintptr_t>(&setVariable), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("getVariable"), { reinterpret_cast<uintptr_t>(&getVariable), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("beginScope"), { reinterpret_cast<uintptr_t>(&beginScope), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("endScope"), { reinterpret_cast<uintptr_t>(&endScope), JITSymbolFlags::Exported }}
            }))) {
        handleAllErrors(std::move(err), [&](ErrorInfoBase& eib) {
            errs() << "Error: " << eib.message() << '\n';
        });
        UnexpectedError("Failed to add symbols to module");
    }
    auto func_symbol = (*jit)->lookup(function_name);
    if (!func_symbol) {
        UnexpectedError("JIT function not found");
    }
    return reinterpret_cast<StmtFunctionType>(func_symbol->getAddress());
}

}