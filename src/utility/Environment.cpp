#include "Environment.hpp"
#include "utility/Report.hpp"

namespace abaci::utility {

void Environment::DefineScope::setType(const std::string& name, const AbaciValue::Type type) {
    auto iter = types.find(name);
    if (iter == types.end()) {
        types.insert({ name, type });
    }
    else {
        UnexpectedError("Variable " + name + " already exists.");
    }
}

AbaciValue::Type Environment::DefineScope::getType(const std::string& name) const {
    auto iter = types.find(name);
    if (iter != types.end()) {
        return iter->second;
    }
    else if (enclosing) {
        return enclosing->getType(name);
    }
    else {
        UnexpectedError("No such variable " + name + ".");
    }
}

bool Environment::DefineScope::isDefined(const std::string& name) const {
    auto iter = types.find(name);
    if (iter != types.end()) {
        return true;
    }
    else if (enclosing) {
        return enclosing->isDefined(name);
    }
    else {
        return false;
    }
}

int Environment::DefineScope::getDepth() const {
    if (!enclosing) {
        return 0;
    }
    else {
        return 1 + enclosing->getDepth();
    }
}

void Environment::Scope::defineValue(const std::string& name, const AbaciValue& value) {
    auto iter = variables.find(name);
    if (iter == variables.end()) {
        variables.insert({ name, value });
    }
    else {
        UnexpectedError("Variable " + name + " already exists.");
    }
}

void Environment::Scope::setValue(const std::string& name, const AbaciValue& value) {
    auto iter = variables.find(name);
    if (iter != variables.end()) {
        if (value.type == iter->second.type) {
            iter->second = value;
        }
        else {
            UnexpectedError("Variable " + name + " already defined with different type.");
        }
    }
    else if (enclosing) {
        enclosing->setValue(name, value);
    }
    else {
        UnexpectedError("Variable " + name + " does not exist.");
    }
}

AbaciValue *Environment::Scope::getValue(const std::string& name) {
    auto iter = variables.find(name);
    if (iter != variables.end()) {
        return &iter->second;
    }
    else if (enclosing) {
        return enclosing->getValue(name);
    }
    else {
       UnexpectedError("No such variable " + name + ".");
    }
}

}
