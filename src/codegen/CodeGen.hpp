#ifndef CodeGen_hpp
#define CodeGen_hpp

#include "ast/Expr.hpp"
#include "ast/Stmt.hpp"
#include "utility/Environment.hpp"
#include "utility/Report.hpp"
#include "engine/JIT.hpp"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <vector>

namespace abaci::codegen {

using llvm::IRBuilder;
using llvm::Module;
using llvm::Value;
using llvm::StructType;
using llvm::BasicBlock;
using abaci::engine::JIT;
using abaci::engine::Cache;
using abaci::utility::AbaciValue;
using abaci::utility::Variable;
using abaci::utility::Environment;
using StackType = std::pair<Value*,AbaciValue::Type>;

class ExprCodeGen {
    JIT& jit;
    IRBuilder<>& builder;
    Module& module;
    mutable std::vector<StackType> stack;
    Environment *environment;
    auto pop() const {
        Assert(!stack.empty())
        auto value = stack.back();
        stack.pop_back();
        return value;
    }
    void push(const StackType& value) const {
        stack.push_back(value);
    }
public:
    ExprCodeGen() = delete;
    ExprCodeGen(JIT& jit) : jit{ jit }, builder{ jit.getBuilder() }, module{ jit.getModule() }, environment{ jit.getEnvironment() } {}
    StackType get() const {
        Assert(stack.size() == 1)
        return stack.front();
    }
    void operator()(const abaci::ast::ExprNode&) const;
    Value *toBoolean(StackType&) const;
    AbaciValue::Type promote(StackType&, StackType&) const;
};

class StmtCodeGen {
    JIT& jit;
    IRBuilder<>& builder;
    Module& module;
    Environment *environment;
    BasicBlock *exitBlock;
    int depth;
public:
    StmtCodeGen() = delete;
    StmtCodeGen(JIT& jit, BasicBlock *exitBlock = nullptr, int depth = -1) : jit{ jit }, builder{ jit.getBuilder() }, module{ jit.getModule() }, environment{ jit.getEnvironment() }, exitBlock{ exitBlock }, depth{ depth } {}
    void operator()(const abaci::ast::StmtList&) const;
    void operator()(const abaci::ast::StmtNode&) const;
private:
    template<typename T>
    void codeGen(const T&) const;
};

class TypeEvalGen {
    mutable std::vector<AbaciValue::Type> stack;
    Environment *environment;
    Cache *cache;
    auto pop() const {
        Assert(!stack.empty())
        auto value = stack.back();
        stack.pop_back();
        return value;
    }
    void push(AbaciValue::Type value) const {
        stack.push_back(value);
    }
public:
    TypeEvalGen() = delete;
    TypeEvalGen(Environment *environment, Cache *cache) : environment{ environment }, cache{ cache } {}
    AbaciValue::Type get() const {
        if (stack.size() != 1) {
            UnexpectedError("Wrong stack size");
        }
        return stack.front();
    }
    void operator()(const abaci::ast::ExprNode&) const;
    AbaciValue::Type promote(AbaciValue::Type, AbaciValue::Type) const;
};

class TypeCodeGen {
    Environment *environment;
    Cache *cache;
    mutable bool type_is_set{ false };
    mutable AbaciValue::Type return_type;
public:
    TypeCodeGen() = delete;
    TypeCodeGen(Environment *environment, Cache *cache) : environment{ environment }, cache{ cache } {}
    void operator()(const abaci::ast::StmtList&) const;
    void operator()(const abaci::ast::StmtNode&) const;
    auto get() const {
        if (type_is_set) {
            return return_type;
        }
        else {
            return AbaciValue::Nil;
        }
    }
private:
    template<typename T>
    void codeGen(const T&) const;
};

}

#endif
