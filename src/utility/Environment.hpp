#ifndef Environment_hpp
#define Environment_hpp

#include "Utility.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include <memory>

namespace abaci::utility {

class Environment {
public:
    class DefineScope {
    public:
        struct Object {
            std::string class_name;
            std::vector<std::variant<AbaciValue::Type,Object>> object_types;
        };
        using Type = std::variant<AbaciValue::Type,Object>;
    private:
        std::unordered_map<std::string,Type> types;
        std::shared_ptr<DefineScope> enclosing;
    public:
        DefineScope(std::shared_ptr<DefineScope> enclosing = nullptr) : enclosing{ enclosing } {}
        void setType(const std::string& name, const Type type);
        Type getType(const std::string& name) const;
        bool isDefined(const std::string& name) const;
        std::shared_ptr<DefineScope> getEnclosing() { return enclosing; }
        int getDepth() const;
    };
    class Scope {
        std::unordered_map<std::string,AbaciValue> variables;
        std::shared_ptr<Scope> enclosing;
    public:
        Scope(std::shared_ptr<Scope> enclosing = nullptr) : enclosing{ enclosing } {}
        void defineValue(const std::string& name, const AbaciValue& value);
        void setValue(const std::string& name, const AbaciValue& value);
        AbaciValue *getValue(const std::string& name);
        std::shared_ptr<Scope> getEnclosing() { return enclosing; }
    };
    Environment() {
        current_define_scope = global_define_scope = std::make_unique<DefineScope>();
        current_scope = std::make_unique<Scope>();
    }
    void beginDefineScope(std::shared_ptr<DefineScope> parent = nullptr) {
        if (parent) {
            current_define_scope = std::make_unique<DefineScope>(parent);
        }
        else {
            current_define_scope = std::make_unique<DefineScope>(current_define_scope);
        }
    }
    void endDefineScope() {
        current_define_scope = current_define_scope->getEnclosing();
    }
    void beginScope() {
        current_scope = std::make_unique<Scope>(current_scope);
    }
    void endScope() {
        current_scope = current_scope->getEnclosing();
    }
    std::shared_ptr<DefineScope> getCurrentDefineScope() { return current_define_scope; }
    std::shared_ptr<DefineScope> getGlobalDefineScope() { return global_define_scope; }
    void setCurrentDefineScope(std::shared_ptr<DefineScope> scope) { current_define_scope = scope; }
    std::shared_ptr<Scope> getCurrentScope() { return current_scope; }
    void reset() {
        while (current_scope->getEnclosing()) {
            endScope();
        }
        while (current_define_scope->getEnclosing()) {
            endDefineScope();
        }
        this_ptrs.clear();
    }
    void setThisPtr(AbaciValue *ptr) { this_ptrs.push_back(ptr); }
    void unsetThisPtr() { this_ptrs.pop_back(); }
    AbaciValue *getThisPtr() { return this_ptrs.empty() ? nullptr : this_ptrs.back(); }
private:
    std::shared_ptr<Scope> current_scope;
    std::shared_ptr<DefineScope> current_define_scope, global_define_scope;
    std::vector<AbaciValue*> this_ptrs;
};

std::string mangled(const std::string& name, const std::vector<Environment::DefineScope::Type>& types);

AbaciValue::Type environmentTypeToType(const Environment::DefineScope::Type& env_type);

bool operator==(const Environment::DefineScope::Type& lhs, const Environment::DefineScope::Type& rhs);

} // namespace abaci::utility

#endif
