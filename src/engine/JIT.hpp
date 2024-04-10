#ifndef JIT_hpp
#define JIT_hpp

#include "utility/Utility.hpp"
#include "utility/Environment.hpp"
#include "utility/Report.hpp"
#include "Cache.hpp"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <string>

namespace abaci::engine {

using llvm::LLVMContext;
using llvm::Module;
using llvm::IRBuilder;
using llvm::StructType;
using llvm::Function;
using llvm::orc::LLJITBuilder;
using llvm::orc::LLJIT;
using abaci::utility::AbaciValue;
using abaci::utility::Environment;
using StmtFunctionType = void(*)();

class JIT {
    std::unique_ptr<LLVMContext> context;
    std::unique_ptr<Module> module;
    IRBuilder<> builder;
    std::string module_name, function_name;
    Function *current_function{ nullptr };
    Environment *environment;
    Cache *cache;
    LLJITBuilder jitBuilder;
    llvm::Expected<std::unique_ptr<LLJIT>> jit{ nullptr };
    void initialize();
public:
    JIT(const std::string& module_name, const std::string& function_name, Environment *environment, Cache *cache)
        : context{ std::make_unique<LLVMContext>() },
        module{ std::make_unique<Module>(module_name, *context) },
        builder{ *context },
        module_name{ module_name }, function_name{ function_name }, environment{ environment }, cache{ cache } {
        initialize();
    }
    auto& getContext() { return *context; }
    auto& getModule() { return *module; }
    auto& getBuilder() { return builder; }
    auto getEnvironment() { return environment; }
    auto getFunction() { return current_function; }
    auto getCache() { return cache; }
    StructType *getNamedType(const std::string& name) const {
        for (auto& type : module->getIdentifiedStructTypes()) {
            if (type->getName().data() == name) {
                return type;
            }
        }
        UnexpectedError("Type not found.")
    }
    StmtFunctionType getExecFunction();
};

}

#endif
