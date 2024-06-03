#include "Environment.hpp"
#include "utility/Report.hpp"
#include "parser/Messages.hpp"
#include <charconv>
#include <cctype>

namespace abaci::utility {

void Environment::DefineScope::setType(const std::string& name, const Environment::DefineScope::Type& type) {
    auto iter = types.find(name);
    if (iter == types.end()) {
        types.insert({ name, type });
    }
    else {
        UnexpectedError1(VarExists, name);
    }
}

Environment::DefineScope::Type Environment::DefineScope::getType(const std::string& name) const {
    auto iter = types.find(name);
    if (iter != types.end()) {
        return iter->second;
    }
    else if (enclosing) {
        return enclosing->getType(name);
    }
    else {
        UnexpectedError1(VarNotExist, name);
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
        UnexpectedError1(VarExists, name);
    }
}

void Environment::Scope::setValue(const std::string& name, const AbaciValue& value) {
    auto iter = variables.find(name);
    if (iter != variables.end()) {
        if (value.type == iter->second.type) {
            iter->second = value;
        }
        else {
            UnexpectedError1(VarType, name);
        }
    }
    else if (enclosing) {
        enclosing->setValue(name, value);
    }
    else {
        UnexpectedError1(VarNotExist, name);
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
        UnexpectedError1(VarNotExist, name);
    }
}

std::string mangled(const std::string& name, const std::vector<Environment::DefineScope::Type>& types) {
    std::string function_name;
    for (unsigned char ch : name) {
        if (ch >= 0x80 || ch == '\'') {
            function_name.push_back('.');
            char buffer[16];
            auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<int>(ch), 16);
            if (ec != std::errc()) {
                UnexpectedError0(BadNumericConv);
            }
            *ptr = '\0';
            function_name.append(buffer);
        }
        else if (isalnum(ch) || ch == '_' || ch == '.') {
            function_name.push_back(ch);
        }
        else {
            UnexpectedError0(BadChar);
        }
    }
    for (const auto& parameter_type : types) {
        function_name.push_back('.');
        if (std::holds_alternative<AbaciValue::Type>(parameter_type)) {
            char buffer[16];
            auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer),
                static_cast<int>(std::get<AbaciValue::Type>(parameter_type) & AbaciValue::TypeMask), 10);
            if (ec != std::errc()) {
                UnexpectedError0(BadNumericConv);
            }
            *ptr = '\0';
            function_name.append(buffer);
        }
        else if (std::holds_alternative<Environment::DefineScope::Object>(parameter_type)) {
            function_name.append(mangled(std::get<Environment::DefineScope::Object>(parameter_type).class_name, {}));
            function_name.push_back('_');
            function_name.append(mangled("", std::get<Environment::DefineScope::Object>(parameter_type).object_types));
            function_name.push_back('_');
        }
    }
    return function_name;
}

AbaciValue::Type environmentTypeToType(const Environment::DefineScope::Type& env_type) {
    if (std::holds_alternative<AbaciValue::Type>(env_type)) {
        return static_cast<AbaciValue::Type>(std::get<AbaciValue::Type>(env_type) & AbaciValue::TypeMask);
    }
    else if (std::holds_alternative<Environment::DefineScope::Object>(env_type)) {
        return AbaciValue::Object;
    }
    else {
        UnexpectedError0(BadType);
    }
}

bool operator==(const Environment::DefineScope::Type& lhs, const Environment::DefineScope::Type& rhs) {
    if (lhs.index() != rhs.index()) {
        return false;
    }
    else if (std::holds_alternative<AbaciValue::Type>(lhs)) {
        return (std::get<AbaciValue::Type>(lhs) & AbaciValue::TypeMask) == (std::get<AbaciValue::Type>(rhs) & AbaciValue::TypeMask);
    }
    else if (std::holds_alternative<Environment::DefineScope::Object>(lhs)) {
        if (std::get<Environment::DefineScope::Object>(lhs).class_name
            != std::get<Environment::DefineScope::Object>(rhs).class_name) {
            return false;
        }
        else if (std::get<Environment::DefineScope::Object>(lhs).object_types.size()
            != std::get<Environment::DefineScope::Object>(rhs).object_types.size()) {
            return false;
        }
        else {
            for (auto lhs_iter = std::get<Environment::DefineScope::Object>(lhs).object_types.cbegin(),
                rhs_iter = std::get<Environment::DefineScope::Object>(rhs).object_types.cbegin();
                lhs_iter != std::get<Environment::DefineScope::Object>(lhs).object_types.end();
                ++lhs_iter, ++rhs_iter) {
                if (!(*lhs_iter == *rhs_iter)) {
                    return false;
                }
            }
            return true;
        }
    }
    UnexpectedError0(BadType);
}

} // namespace abaci::utility
