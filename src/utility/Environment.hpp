#ifndef Environment_hpp
#define Environment_hpp

#include "Utility.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace abaci::utility {

class Environment {
public:
    class DefineScope {
        std::unordered_map<std::string,AbaciValue::Type> types;
        std::shared_ptr<DefineScope> enclosing;
    public:
        DefineScope(std::shared_ptr<DefineScope> enclosing = nullptr) : enclosing{ enclosing } {}
        void setType(const std::string& name, const AbaciValue::Type type);
        AbaciValue::Type getType(const std::string& name) const;
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
private:
    std::shared_ptr<Scope> current_scope;
    std::shared_ptr<DefineScope> current_define_scope, global_define_scope;
};

} // namespace abaci::utility

#endif
