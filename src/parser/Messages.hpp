#ifndef Messages_hpp
#define Messages_hpp

#define MESSAGE(MSG, STR) inline const char *MSG = reinterpret_cast<const char*>(u8##STR)

// lib/Abaci.cpp
MESSAGE(InstanceOf, "<Instance of {}>");
MESSAGE(UnknownType, "Unknown type ({}).");
MESSAGE(BadOperator, "Unknown operator in this context.");
MESSAGE(BadConvType, "Bad type for conversion to \'{}\'.");
MESSAGE(NeedType, "Must be \'{}\' type.");
MESSAGE(BadConvTarget, "Bad target conversion type ({}).");
// utility/Environment.cpp
MESSAGE(VarExists, "Variable \'{}\' already exists.");
MESSAGE(VarNotExist, "Variable \'{}\' does not exist.");
MESSAGE(VarType, "Existing variable \'{}\' has different type.");
MESSAGE(BadNumericConv, "Bad numeric conversion when generating mangled name.");
MESSAGE(BadChar, "Bad character in function name.");
MESSAGE(BadType, "Bad type.");
// engine/Cache.cpp
MESSAGE(ClassExists, "Class \'{}\' already exists.");
MESSAGE(FuncExists, "Function \'{}\' already exists.");
MESSAGE(WrongArgs, "Wrong number of arguments (have {}, need {}).");
MESSAGE(FuncNotExist, "Function \'{}\' does not exist.");
MESSAGE(NoInst, "No such instantiation for function \'{}\'.");
MESSAGE(ClassNotExist, "Class \'{}\' does not exist.");
MESSAGE(DataNotExist, "Object does not have data member \'{}\'.");
// engine/JIT.hpp
MESSAGE(NoType, "Type \'{}\' not found.");
// engine/JIT.cpp
MESSAGE(NoLLJIT, "Failed to create LLJIT instance.");
MESSAGE(NoModule, "Failed to add IR module.");
MESSAGE(NoSymbol, "Failed to add symbols to module.");
MESSAGE(NoJITFunc, "JIT function not found.");
// codegen/ExprCodeGen.cpp
MESSAGE(NoAssignObject, "Cannot assign objects.");
MESSAGE(CallableNotExist, "No function or class called \'{}\'.");
MESSAGE(BadObject, "Not an object.");
MESSAGE(BadConvSource, "Bad source conversion type.");
MESSAGE(BadAssociation, "Unknown association type.");
MESSAGE(BadNode, "Bad node type.");
MESSAGE(BadCoerceTypes, "Incompatible types.");
MESSAGE(NoObject, "Operation is incompatible with object type.");
MESSAGE(NoBoolean, "Cannot convert this type to Boolean.");
// codegen/StmtCodeGen.cpp
MESSAGE(BadPrint, "Bad print entity.");
MESSAGE(NoConstantAssign, "Cannot reassign to constant \'{}\'.");
MESSAGE(DataType, "Data member already has different type.");
MESSAGE(BadStmtNode, "Bad StmtNode type.");
// codegen/TypeCodeGen.cpp
MESSAGE(ReturnAtEnd, "Return statement must be at end of block.");
MESSAGE(ObjectType, "Existing object \'{}\' has different type(s).");
MESSAGE(FuncTopLevel, "Functions must be defined at top-level.");
MESSAGE(ReturnOnlyInFunc, "Return statement can only appear inside a function.");
MESSAGE(FuncTypeSet, "Function return type already set to different type.");
MESSAGE(NoExpression, "Expression not permitted in this context.");
// main.cpp
MESSAGE(BadParse, "Could not parse file.");
MESSAGE(InitialPrompt, "Abaci0 version {}\nEnter code, or a blank line to end:\n> ");
MESSAGE(InputPrompt, "> ");
MESSAGE(ContinuationPrompt, ". ");
MESSAGE(SyntaxError, "Syntax error.");
MESSAGE(Version, "1.0.2 (2024-Jun-22)");

#endif
