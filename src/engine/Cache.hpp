#ifndef Cache_hpp
#define Cache_hpp

#include "ast/Stmt.hpp"
#include "utility/Utility.hpp"
#include "utility/Environment.hpp"
#include <stdexcept>
#include <unordered_map>
#include <string>
#include <vector>
#include <utility>

namespace abaci::engine {

using abaci::ast::Variable;
using abaci::ast::StmtList;
using abaci::utility::AbaciValue;
using abaci::utility::Environment;

class Cache {
public:
    struct Class {
        std::vector<Variable> variables;
        std::vector<std::string> methods;
    };
    struct Function {
        std::vector<Variable> parameters;
        StmtList body;
    };
    struct Instantiation {
        std::string name;
        std::vector<Environment::DefineScope::Type> parameter_types;
        Environment::DefineScope::Type return_type;
        std::shared_ptr<Environment::DefineScope> scope;
    };
    enum Type{ CacheClass, CacheFunction, CacheNone };
    Cache() = default;
    void addClassTemplate(const std::string& name, const std::vector<Variable>& variables, const std::vector<std::string>& methods);
    void addFunctionTemplate(const std::string& name, const std::vector<Variable>& parameters, const StmtList& body);
    void addFunctionInstantiation(const std::string& name, const std::vector<Environment::DefineScope::Type>& types, Environment *environment);
    Environment::DefineScope::Type getFunctionInstantiationType(const std::string& name, const std::vector<Environment::DefineScope::Type>& types) const;
    std::shared_ptr<Environment::DefineScope> getFunctionInstantiationScope(const std::string& name, const std::vector<Environment::DefineScope::Type>& types) const;
    Type getCacheType(const std::string& name) const;
    const Function& getFunction(const std::string& name) const;
    const Class& getClass(const std::string& name) const;
    unsigned getMemberIndex(const Class& cache_class, const Variable& member) const;
    const auto& getInstantiations() const { return instantiations; }
    void clearInstantiations() { instantiations.clear(); }
private:
    std::unordered_map<std::string,Class> classes;
    std::unordered_map<std::string,Function> functions;
    std::vector<Instantiation> instantiations;
};

} // namespace abaci::engine

#endif
