#ifndef Abaci_hpp
#define Abaci_hpp

#include "utility/Utility.hpp"
#include "utility/Environment.hpp"

extern "C" {

void printValue(abaci::utility::AbaciValue *value);

void printComma();

void printLn();

void complexMath(abaci::utility::Complex *result, abaci::utility::Operator op, abaci::utility::Complex *operand1, abaci::utility::Complex *operand2 = nullptr);

void setVariable(abaci::utility::Environment *environment, char *name, abaci::utility::AbaciValue *value, bool new_variable);

abaci::utility::AbaciValue *getVariable(abaci::utility::Environment *environment, char *name);

void setObjectData(abaci::utility::Environment *environment, char *name, int *indices, abaci::utility::AbaciValue *value);

abaci::utility::AbaciValue *getObjectData(abaci::utility::Environment *environment, char *name, int *indices);

void beginScope(abaci::utility::Environment *environment);

void endScope(abaci::utility::Environment *environment);

void setThisPtr(abaci::utility::Environment *environment, abaci::utility::AbaciValue *ptr);

void unsetThisPtr(abaci::utility::Environment *environment);

void getUserInput(abaci::utility::String *str);

void convertType(abaci::utility::AbaciValue *to, abaci::utility::AbaciValue *from);

}

#endif
