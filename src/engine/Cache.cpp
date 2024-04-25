#include "Cache.hpp"
#include "codegen/CodeGen.hpp"
#include "utility/Report.hpp"
#include <algorithm>

namespace abaci::engine {

using abaci::codegen::TypeCodeGen;

void Cache::addClassTemplate(const std::string& name, const std::vector<Variable>& variables, const std::vector<std::string>& methods) {
    auto iter = classes.find(name);
    if (iter == classes.end()) {
        classes.insert({ name, { variables, methods }});
    }
    else {
        UnexpectedError("Class already exists.");
    }
}

void Cache::addFunctionTemplate(const std::string& name, const std::vector<Variable>& parameters, const StmtList& body) {
    auto iter = functions.find(name);
    if (iter == functions.end()) {
        functions.insert({ name, { parameters, body }});
    }
    else {
        UnexpectedError("Function already defined.");
    }
}

void Cache::addFunctionInstantiation(const std::string& name, const std::vector<AbaciValue::Type>& types, Environment *environment) {
    auto iter = functions.find(name);
    if (iter != functions.end()) {
        if (types.size() == iter->second.parameters.size()) {
            bool create_instantiation = true;
            auto mangled_name = mangled(name, types);
            for (const auto& instantiation : instantiations) {
                if (mangled_name == mangled(instantiation.name, instantiation.parameter_types)) {
                    create_instantiation = false;
                    break;
                }
            }
            if (create_instantiation) {
                instantiations.push_back({ name, types, AbaciValue::Unset, nullptr });
                TypeCodeGen gen_return_type(environment, this, true);
                gen_return_type(iter->second.body);
                auto iter = std::find_if(instantiations.begin(), instantiations.end(),
                        [&](const auto& elem){
                            return mangled(elem.name, elem.parameter_types) == mangled(name, types);
                        });
                if (iter != instantiations.end()) {
                    instantiations.erase(iter);
                }
                instantiations.push_back({ name, types, gen_return_type.get(), environment->getCurrentDefineScope() });
            }
        }
        else {
            LogicError("Wrong number of arguments.");
        }
    }
    else {
        LogicError("No such function.");
    }
}

AbaciValue::Type Cache::getFunctionInstantiationType(const std::string& name, const std::vector<AbaciValue::Type>& types) const {
    auto mangled_name = mangled(name, types);
    for (const auto& instantiation : instantiations) {
        if (mangled_name == mangled(instantiation.name, instantiation.parameter_types)) {
            return instantiation.return_type;
        }
    }
    UnexpectedError("No such function instantiation.");
}

std::shared_ptr<Environment::DefineScope> Cache::getFunctionInstantiationScope(const std::string& name, const std::vector<AbaciValue::Type>& types) const {
    auto mangled_name = mangled(name, types);
    for (const auto& instantiation : instantiations) {
        if (mangled_name == mangled(instantiation.name, instantiation.parameter_types)) {
            return instantiation.scope;
        }
    }
    UnexpectedError("No such function instantiation.");
}

const Cache::Function& Cache::getFunction(const std::string& name) const {
    auto iter = functions.find(name);
    if (iter != functions.end()) {
        return iter->second;
    }
    UnexpectedError("No such function.");
}

const Cache::Class& Cache::getClass(const std::string& name) const {
    auto iter = classes.find(name);
    if (iter != classes.end()) {
        return iter->second;
    }
    UnexpectedError("No such class.");
}

Cache::Type Cache::getCacheType(const std::string& name) const {
    auto iter1 = functions.find(name);
    if (iter1 != functions.end()) {
        return CacheFunction;
    }
    auto iter2 = classes.find(name);
    if (iter2 != classes.end()) {
        return CacheClass;
    }
    return CacheNone;
}

} // namespace abaci::engine
