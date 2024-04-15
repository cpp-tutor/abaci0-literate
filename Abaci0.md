# Abaci0: A JIT-compiled, statically-typed, and interactive scripting language

*Programing language design has to be an art as well as a science. The ability to appeal to (human) coders is not something which always comes naturally to computer scientists, while the science element may be too technically complex for the amateur coder to venture into. Surely the skill lies in compromising the least to both of accessibility and implementation limitations.*

*Abaci0 achieves this by having several, usually contradictory, features. It is statically typed, but variable assignments and function calls do not require type annotations. It is whitespace-neutral, without requiring semi-colons or other statement delimiters. It does not have a hand-written scanner/parser, or even a machine generated one, relying on the Boost Spirit X3 library to parse input. It is not another transpiler (to C or JavaScript), but produces native code dynamically via the LLVM backend.*

The source code for this program is organized in a logical fashion, with six directories each containing a small number of C++ header and implementation files. In general, all of the code within each file is within a namespace named after the directory, for example `abaci::ast`&mdash;the major exceptions to this convention are the files `lib/Abaci.hpp` and `lib/Abaci.cpp`, where the functions declared are at global scope (and use C-linkage) for ease of referencing within the generated code.

The source code presented in this literate program is discussed in the following sequence:

* `utility`: Code to facilitate the static type system and symbol table, manage lexical scoping and report errors with a source location.
* `lib`: Functions directly exposed to the JIT engine at runtime, depending upon the source files in `utility` as well as the C++ standard library and libfmt.
* `ast`: Two C++ headers which define all of the classes used to hold the Abstract Syntax Tree.
* `parser`: All of the Boost Spirit X3 parsing code logic is within this directory, together with a header file declaring all of the Abaci keywords and other symbols.
* `codegen`: Translation of the constructed AST into LLVM API calls is performed by the code within this directory.
* `engine`: Handles management of the function cache and creation of a standalone function to execute the generated code.
* `main.cpp`: The simple frontend and driver presented allows per-file and per-statement JIT compilation and execution.

Notes added to the descriptions take the form [Note: ...] and may be of a different style to the rest of the discussion. They are often intended to clarify the state of functionality and completeness of the project so far, as well as outline the future direction of code improvements.

## Directory `utility`

### `Utility.hpp`

The class `AbaciValue` is the foundation of the static type system for Abaci0, providing support for seven different types. [Note: Support for `Object` is not yet implemented.] The basis for the class is a "tagged union" where the size of the `value` union is guaranteed to be 64 bits wide. The `type` field is based upon a (plain) enum, and is always initialized when an `AbaciValue` is first created. [Note: The need for this type field is possibly questionable as Abaci0 is a statically-typed language and type information for each symbol is held in the various `DefineScope`s constructed at compile-time. A future improvement could be to omit this field, however it is not clear whether this is technically possible.]

```cpp
#ifndef Utility_hpp
#define Utility_hpp

#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <ostream>
#include <cstring>

namespace abaci::utility {

struct AbaciValue;

```

The `Complex` and `String` types are intended to be able to be created using LLVM API calls and so are plain structs, with two constructors defined for `String`&mdash;one for the parser to create string constants and one for use by the `clone()` function. Class `Variable` is a wrapper around a `std::string` and exists to hold a variable name. [Note: `Object` contains named member variables and functions as C++ library types and would need to be initialized by a client function in `lib/Abaci.cpp`.]

```cpp
struct Complex {
    double real{}, imag{};
};

struct String {
    char8_t *ptr{ nullptr };
    std::size_t len{};
    String(const std::string& s) {
        ptr = new char8_t[s.size() + 1];
        strcpy(reinterpret_cast<char*>(ptr), s.c_str());
        len = s.size();
    }
    String(const char8_t *p, std::size_t l) {
        ptr = new char8_t[l + 1];
        strcpy(reinterpret_cast<char*>(ptr), reinterpret_cast<const char*>(p));
        len = l;
    }
    ~String() { delete[] ptr; ptr = nullptr; len = 0; }
};

class Variable {
    std::string name;
public:
    Variable() = default;
    Variable(const Variable&) = default;
    Variable& operator=(const Variable&) = default;
    Variable(const std::string& name) : name{ name } {}
    const auto& get() const { return name; }
};

struct Object {
    std::vector<std::pair<Variable,AbaciValue>> variables;
    std::vector<std::string> methods;
};

```

Class `AbaciValue`'s `Type` enum lists all of the possible types (in approximate order of "complexity"), as well as a `Constant` bit used for `let =`, and a `TypeMask` to mask out this bit when constants are dereferenced. The `value` union is intended to be set to all zero bits when an `AbaciValue` is created; setting of the `type` field is always mandatory. The default constructor creates a `Nil` type, the other five are declared `explicit` to avoid unwanted type conversions; these constructors are only intended to be called from the parser to utilize constant values declared within the Abaci0 program. The `value.integer` field stores 64-bit integers in unsigned form even though signed math takes place during code generation; this allows input and correct parsing of large positive values (such as `0x8000000000000000`) which are then treated as two's-complement signed.

```cpp
struct AbaciValue {
    enum Type { Nil, Boolean, Integer, Float, Complex, String, Object, TypeMask = 15, Constant = 16 };
    union {
        void *nil{ nullptr };
        bool boolean;
        unsigned long long integer;
        double floating;
        abaci::utility::Complex *complex;
        abaci::utility::String *str;
        abaci::utility::Object *object;
    } value;
    Type type;
    AbaciValue() : type{ Nil } {}
    explicit AbaciValue(bool b) : type{ Boolean } { value.boolean = b; }
    explicit AbaciValue(unsigned long long ull) : type{ Integer } { value.integer = ull; }
    explicit AbaciValue(double d) : type{ Float } { value.floating = d; }
    explicit AbaciValue(double real, double imag) : type{ Complex } { value.complex = new abaci::utility::Complex{ real, imag }; }
```

The copy and copy-assignment constructors perform a deep copy on types that need it (`Complex` and `String`), calling private member function `clone()` to do this. The destructor is required in order to correctly release memory used by these types.

```cpp
    explicit AbaciValue(const std::string& s) : type{ String } { value.str = new abaci::utility::String(s); }
    AbaciValue(const AbaciValue& rhs) { clone(rhs); }
    AbaciValue& operator=(const AbaciValue& rhs) { clone(rhs); return *this; }
    ~AbaciValue();
private:
    void clone(const AbaciValue&);
};

static_assert(sizeof(AbaciValue::value) == 8, "AbaciValue::value must be exactly 64 bits");

```

The scoped enum `Operator` lists all of the operators used by Abaci0. A mapping of the string values to these tokens is held in the map `Operators`, with the string values as the keys. The function `mangled()` takes a function name and its input parameter types and returns a unique mangled name, used for Abaci0 function instantiation as unique LLVM functions. Stream output for `AbaciValue` and `Operator`, and generic conversion of any type to a string representation according to its stream output form, are supported.

```cpp
enum class Operator { None, Plus, Minus, Times, Divide, Modulo, FloorDivide, Exponent,
    Equal, NotEqual, Less, LessEqual, GreaterEqual, Greater,
    Not, And, Or, Compl, BitAnd, BitOr, BitXor,
    Comma, SemiColon, Assign, To };

extern const std::unordered_map<std::string,Operator> Operators;

std::string mangled(const std::string& name, const std::vector<AbaciValue::Type>& types);

}

std::ostream& operator<<(std::ostream& os, const abaci::utility::AbaciValue&);

std::ostream& operator<<(std::ostream& os, const abaci::utility::Operator);

template<typename T>
std::string toString(const T& value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

#endif
```

### `Utility.cpp`

Global container `Operators` is defined in this implementation file as a `std::unordered_map`, with the keys being string representations defined in `parser/Keywords.hpp` and the only client being `parser/Parse.cpp`.

```cpp
#include "Utility.hpp"
#include "parser/Keywords.hpp"
#include <iomanip>

namespace abaci::utility {

const std::unordered_map<std::string,Operator> Operators{
    { PLUS, Operator::Plus },
    { MINUS, Operator::Minus },
    { TIMES, Operator::Times },
    { DIVIDE, Operator::Divide },
    { MODULO, Operator::Modulo },
    { FLOOR_DIVIDE, Operator::FloorDivide },
    { EXPONENT, Operator::Exponent },
    { EQUAL, Operator::Equal },
    { NOT_EQUAL, Operator::NotEqual },
    { LESS, Operator::Less },
    { LESS_EQUAL, Operator::LessEqual },
    { GREATER_EQUAL, Operator::GreaterEqual },
    { GREATER, Operator::Greater },
    { NOT, Operator::Not },
    { AND, Operator::And },
    { OR, Operator::Or },
    { BITWISE_COMPL, Operator::Compl },
    { BITWISE_AND, Operator::BitAnd },
    { BITWISE_OR, Operator::BitOr },
    { BITWISE_XOR, Operator::BitXor },
    { COMMA, Operator::Comma },
    { SEMICOLON, Operator::SemiColon },
    { ASSIGN, Operator::Assign },
    { TO, Operator::To }
};

```

Function `AbaciValue::clone()` needs to mask out the `Constant` bit before performing a deep copy, ensuring to not copy null `Complex` or `String` types other than as `nullptr`.

```cpp
void AbaciValue::clone(const AbaciValue& rhs) {
    switch (rhs.type & TypeMask) {
        case Nil:
            break;
        case Boolean:
            value.boolean = rhs.value.boolean;
            break;
        case Integer:
            value.integer = rhs.value.integer;
            break;
        case Float:
            value.floating = rhs.value.floating;
            break;
        case Complex:
            value.complex = rhs.value.complex ? new abaci::utility::Complex{ rhs.value.complex->real, rhs.value.complex->imag } : nullptr;
            break;
        case String:
            value.str = rhs.value.str ? new abaci::utility::String(rhs.value.str->ptr, rhs.value.str->len) : nullptr;
            break;
        case Object:
            value.object = rhs.value.object ? new abaci::utility::Object{ rhs.value.object->variables, rhs.value.object->methods } : nullptr;
            break;
        default:
            break;
    }
    type = rhs.type;
}

```
Similarly the destructor needs only perform cleanup on types which allocate heap memory.

```cpp
AbaciValue::~AbaciValue() {
    switch (type & TypeMask) {
        case Complex:
            delete value.complex;
            value.complex = nullptr;
            break;
        case String:
            delete value.str;
            value.str = nullptr;
            break;
        case Object:
            delete value.object;
            value.object = nullptr;
            break;
        default:
            break;
    }
}

```

Global function `mangled()` creates a unique function name based upon the types of its parameters. A call to `f(a,b)` where `a` is an `AbaciValue::Integer` and `b` is an `AbaciValue::Floating` would be given the name `_f_2_3`. This function is used to allow per-type function instantiation by `engine/Cache.cpp` and `engine/JIT.cpp`.

```cpp
std::string mangled(const std::string& name, const std::vector<AbaciValue::Type>& types) {
    std::string function_name{ "_" };
    function_name.append(name);
    for (const auto parameter_type : types) {
        function_name.append("_");
        function_name.append(std::to_string(parameter_type));
    }
    return function_name;
}

}

```

Stream output for `AbaciValue` objects uses tokens defined in `parser/Keywords.hpp`, with floating-point values output to ten significant digits in general form. The `Constant` bit is again masked out.

```cpp
std::ostream& operator<<(std::ostream& os, const abaci::utility::AbaciValue& value) {
    using abaci::utility::AbaciValue;
    switch (value.type & AbaciValue::TypeMask) {
        case AbaciValue::Nil:
            return os << NIL;
        case AbaciValue::Boolean:
            return os << (value.value.boolean ? TRUE : FALSE);
        case AbaciValue::Integer:
            return os << static_cast<long long>(value.value.integer);
        case AbaciValue::Float:
            return os << std::setprecision(10) << value.value.floating;
        case AbaciValue::Complex:
            return os << value.value.complex->real << (value.value.complex->imag >= 0 ? "+" : "") << value.value.complex->imag << IMAGINARY1;
        case AbaciValue::String:
            return os.write(reinterpret_cast<const char*>(value.value.str->ptr), value.value.str->len);
        default:
            return os << value.type << '?';
    }
}

```

Stream output of `Operator` tokens is supported which was previously used for AST printing.

```cpp
std::ostream& operator<<(std::ostream& os, const abaci::utility::Operator op) {
    for (const auto& item : abaci::utility::Operators) {
        if (item.second == op) {
            return os << '(' << item.first << ')';
        }
    }
    return os << "(?)";
}
```

### `Environment.hpp`

The class `Environment` forms the basis for lexical and per-function scoping in the Abaci0 language. Only a single instance of `Environment` is intended to be created for a program session (per-statement or whole-file compilation) and this object holds the state of the program during parsing and execution. The two types of state are distinct, only type information for each scope is needed during parsing and type-checking, and this is held in `Environment::DefineScope` instances, while value and type information held in `Environment::Scope` instances is used by the running program (via calls to functions declared in `lib/Abaci.hpp`).

Both class `Environment::DefineScope` and class `Environment::Scope` hold a shared pointer to their parent, which is `nullptr` in the case of global scope. `Environment`'s `current_define_scope`, `global_define_scope` and `current_scope` are initialized in the constructor as global scope holders. Method `Environment::beginDefineScope()` creates a new sub-scope with either a named parent as the enclosing scope, or the current scope as the enclosing scope, assigning this to `current_define_scope`. Method `Environment::endDefineScope()` sets the `current_define_scope` to its enclosing scope, causing the ending scope to be deleted as no more shared pointers will refer to it. Methods `Environment::beginScope()` and `Environment::endScope()` provide similar behaviour for runtime scopes.

```cpp
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

}

#endif
```

The purpose of `Environment::setCurrentDefineScope()` is to allow function instantiations to only access global and parameter variables, thus the `DefineScope` is a subset of the `Scope` and local functions are therefore disallowed. The `DefineScope` needs to be reset (using a call to the same function) once the function instantiation is complete.

### `Environment.cpp`

Method `setType()` only allows creation of new variables in the current `DefineScope`, and reports an error if it finds one with the same name.

```cpp
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

```

Method `getType()` searches recursively down the `DefineScope` hierarchy for a variable with the required name, stopping and reporting an error only at global scope.

```cpp
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

```

Method `isDefined()` allows the code generation functions to trap the error that would be generated by `getType()` on a non-existent variable.

```cpp
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

```

Method `getDepth()` returns a nesting depth (starting at zero) of the current `DefineScope`, which is used by Abaci0 return statements within lexical scopes, to generate the correct number of runtime library `endScope()`  calls.

```cpp
int Environment::DefineScope::getDepth() const {
    if (!enclosing) {
        return 0;
    }
    else {
        return 1 + enclosing->getDepth();
    }
}

```

Moving on to `Scope` methods, `defineValue()` sets the initial value of a variable in the current scope, reporting an error if a variable already exists.

```cpp
void Environment::Scope::defineValue(const std::string& name, const AbaciValue& value) {
    auto iter = variables.find(name);
    if (iter == variables.end()) {
        variables.insert({ name, value });
    }
    else {
        UnexpectedError("Variable " + name + " already exists.");
    }
}

```

Method `setValue()` recursively looks for a variable with the requested name, reporting an error if none is found, or the existing variable has a different type.

```cpp
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

```

Method `getValue()` returns the address of a variable with the requested name, reporting an error if none is found. The fact that no new object is created (returned) is the basis for the memory management of the Abaci0 JIT, which uses the symbol table in the `Environment::Scope` as the method of releasing memory used by variables going out of scope.

```cpp
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
```

### `Report.hpp`

The error class `AbaciError` and its sub-classes `CompilerError` and `AssertError` inherit from `std::exception`, overriding the `what()` function to provide error diagnostics in client code's `catch` clause. Both `CompilerError` and `AssertError` store the source filename and line number in the protected `message` member of `AbaciError`.

```cpp
#ifndef Report_hpp
#define Report_hpp

#include <exception>
#include <string>

class AbaciError : public std::exception {
protected:
    std::string message{};
public:
    AbaciError(const std::string& message = "") : message{ message } {}
    virtual const char *what() const noexcept override { return message.c_str(); }
    virtual ~AbaciError() {}
};

class CompilerError : public AbaciError {
public:
    CompilerError(const std::string& error_string, const char* source_file = "", const int line_number = -1)
        : AbaciError("Compiler inconsistency detected: ")
    {
        message.append(error_string);
        if (source_file != std::string{}) {
            message.append("\nSource filename: ");
            message.append(source_file);
            if (line_number != -1) {
                message.append(", line: ");
                message.append(std::to_string(line_number));
            }
        }
    }
};

class AssertError : public AbaciError {
public:
    AssertError(const std::string& assertion, const char* source_file = "", const int line_number = -1)
        : AbaciError("Assertion failed: ")
    {
        message.append(assertion);
        if (source_file != std::string{}) {
            message.append("\nSource filename: ");
            message.append(source_file);
            if (line_number != -1) {
                message.append(", Line number: ");
                message.append(std::to_string(line_number));
            }
        }
    }
};

```

Convenience macros `LogicError()`, `UnexpectedError()` and `Assert()` allow for population of all of the constructor fields of these classes. `LogicError()` is intended for reporting errors due to ill-formed Abaci0 programs, while `UnexpectedError()` and `Assert()` are intended to pinpoint problems in the compiler itself. [Note: Adaptation of the source code to prefer `LogicError()` is not yet complete.]

```cpp
#define LogicError(error_string) { throw AbaciError(error_string); }
#define UnexpectedError(error_string) { throw CompilerError(error_string, __FILE__, __LINE__); }
#define Assert(condition) { if (!(condition)) throw AssertError(#condition, __FILE__, __LINE__); }

#endif
```

## Directory `lib`

### `Abaci.hpp`

The functionality of the compiler which needs to be made available to the JIT-compiled code at execution time are listed in this header file. The functions are C++ functions declared with C-linkage, ensuring that the unmangled names can be used in the client code. This library is intentionally small, with three output-related functions, one related to operations on complex numbers, and four functions related to environment (specifically `Environment::Scope`) operations.

```cpp
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

void beginScope(abaci::utility::Environment *environment);

void endScope(abaci::utility::Environment *environment);

}

#endif
```

### `Abaci.cpp`

Function `printValue()` allows the compiled code to output variables at execution time. The `print(std::ostream)` function from libfmt is used for this task; calling `toString()` produces a `std::string` representation from the stream operator. [Note: Ideally a custom formatter would be used, but issues with the flexibility of `AbaciValue()` and its value layout caused problems with the `constexpr` requirements of such a formatter. However, with the type of the value being output always known at code compilation time, this issue may be addressable.]

```cpp
#include "Abaci.hpp"
#include "utility/Report.hpp"
#include <complex>
#include <cstring>
#include <algorithm>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <iostream>
using fmt::print;

using abaci::utility::AbaciValue;
using abaci::utility::Complex;
using abaci::utility::Operator;
using abaci::utility::String;
using abaci::utility::Environment;

void printValue(AbaciValue *value) {
    print(std::cout, "{}", toString(*value));
}

```

Function `printComma()` is intended to pad and separate fields after a `print Something,` call (where more than one comma is permitted), however currently only a single space character is output. Function `printLn()` simply outputs a newline character, this is suppressed with `print Something;`.

```cpp
void printComma() {
    print(std::cout, "{}", ' ');
}

void printLn() {
    print(std::cout, "{}", '\n');
}

```

Support for operations on type `abaci::utility::Complex` are provided by function `complexMath()`. Even though this struct is likely to be bit-compatible with `std::complex<double>`, this is not relied upon. Creation of `std::complex<double>` from `operand1` and `operand2` are performed, followed by an arithmetic operation specified by `op` (of type `abaci::utility::Operator`). The result is then copied into an uninitialized `Complex` which is assumed alloca'd by the caller (this is the memory management technique used to avoid leaks).

```cpp
void complexMath(Complex *result, Operator op, Complex *operand1, Complex *operand2) {
    std::complex<double> a(operand1->real, operand1->imag), b, r;
    if (operand2) {
        b = std::complex<double>(operand2->real, operand2->imag);
    }
    switch (op) {
        case Operator::Plus:
            r = a + b;
            break;
        case Operator::Minus:
            if (operand2) {
                r = a - b;
            }
            else {
                r = -a;
            }
            break;
        case Operator::Times:
            r = a * b;
            break;
        case Operator::Divide:
            r = a / b;
            break;
        case Operator::Exponent:
            r = std::pow(a, b);
            break;
        default:
            UnexpectedError("Unknown operator.");
    }
    result->real = r.real();
    result->imag = r.imag();
}

```

Functions `setVariable()` and `getVariable()` call member functions of the `environment` parameter, with `new_variable` being known and fixed at code compilation time. All parameters other than `new_variable` are passed and returned by pointer. Functions `beginScope()` and `endScope()` are called by the compiled code when entering and leaving a lexical scope, respectively.

```cpp
void setVariable(Environment *environment, char *name, AbaciValue *value, bool new_variable) {
    if (new_variable) {
        environment->getCurrentScope()->defineValue(name, *value);
    }
    else {
        environment->getCurrentScope()->setValue(name, *value);
    }
}

AbaciValue *getVariable(Environment *environment, char *name) {
    return environment->getCurrentScope()->getValue(name);
}

void beginScope(Environment *environment) {
    environment->beginScope();
}

void endScope(Environment *environment) {
    environment->endScope();
}
```

## Directory `ast`

An Abstract Syntax Tree (AST) representation of a program traditionally takes the form of a binary tree structure, often using only raw pointers and no memory release, making them unsuitable for use in a REPL. In this literate program we present an alternative using only C++ standard containers (`std::vector`, `std::variant`) and demonstrate a type- and memory-safe alternative to pointer-based trees.

The two header files in this directory are designed to be used by the parser and code generation stages, the information held in the relevant classes being the (only) link between program analysis and code synthesis, as for a traditional tree structure.

### `Expr.hpp`

The need for expressions in program logic is difficult to over-emphasise, so these are usually catered for by making them distinct from statements. The class `ExprNode` is intended to hold a single piece of the AST, but being able (by use of `ExprList = std::vector<ExprNode>`) to reference an arbitrary number of sub-nodes. In most cases an `ExprList` will contain only one (or two) sub-node(s), due to the way expressions are constructed from high-precedence (innermost) to low-precedence (outermost), but the flexibility allows efficient code generation for constructs such as `1 + 3 - 2` and `0 <= n < 10`.

The basis for the design of class `ExprNode` is the variant container type `std::variant<AbaciValue,Operator,ExprList,Variable,ValueCall>`. To use this with an X3 parser the Boost Fusion library would typically be employed, however the requirement of the parser constructing a tree with different precedencies and associations (left, right, unary, none) means that this approach is unsatisfactory. To summarize, X3 rules would try to flatten the input to a sequence of `AbaciValue`s, `Variable`s, `Operator`s etc. into an `ExprList`, losing the precedence information in the process. Also, the association of the rule could not be passed into the `ExprList` as none of the rule components would contain this information.

The solution chosen was to abandon Boost Fusion (along with `x3::variant`) and instead create generic copy constructors and copy assignment constructors which would populate the correct field of the `std::variant` outlined above. (These steps appear to be all which is needed to make a C++ class compatible with X3.) It is still the responsibility of the X3 rule and its helper functions to correctly set the association of any `ExprList` containing an `ExprNode`, while none of the other types use it at all, which may appear incongruous at first glance.

Definition of this class begins with a forward-declaration and definitions of all of the possible types of the `std::variant`.

```cpp
#ifndef Expr_hpp
#define Expr_hpp

#include "utility/Utility.hpp"
#include <boost/fusion/adapted/struct.hpp>
#include <vector>
#include <variant>
#include <utility>

namespace abaci::ast {

class ExprNode;

using ExprList = std::vector<ExprNode>;

using abaci::utility::AbaciValue;
using abaci::utility::Operator;
using abaci::utility::Variable;

struct ValueCall {
    std::string name;
    ExprList args;
};

```

The class definition itself begins with two plain enums, `Association` and `Type` (which holds the same order as types specified in the `std::variant`), followed by defaulted constructors.

```cpp
class ExprNode {
public:
    enum Association { Unset, Left, Right, Unary, Boolean };
    enum Type { ValueNode, OperatorNode, ListNode, VariableNode, CallNode };
    ExprNode() = default;
    ExprNode(const ExprNode&) = default;
    ExprNode& operator=(const ExprNode&) = default;
```

Then come the generic constructors, intended to be used with any type that the `std::variant` can hold, setting the association to `Unset`.

```cpp
    template<typename T>
    explicit ExprNode(const T& data, Association association = Unset) : data{ data }, association{ association } {}
    template<typename T>
    ExprNode& operator=(const T& d) { data = d; association = Unset; return *this; }
```

The "getters" and private member variables complete the class definition. The switch statements in `codegen/ExprCodeGen.cpp` are based upon querying these two methods.

```cpp
    Association getAssociation() const { return association; }
    const auto& get() const { return data; }
private:
    std::variant<AbaciValue,Operator,ExprList,Variable,ValueCall> data;
    Association association{ Unset };
};

}

```

The class `ValueCall` is then adapted by Boost Fusion to be able to be used with an X3 rule&mdash;this takes place at global (not namespace) scope.

```cpp
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::ValueCall, name, args)

#endif
```

### `Stmt.hpp`

A slightly simpler problem presents itself for statements generated from X3 rules&mdash;the need for polymorphism. Put simply, the X3 `<any statement>` rule must match any `StmtNode`, but it needs to be also distinguished as one of `PrintStmt`, `InitStmt` etc. A `std::variant` could be employed, but would contain many different fields as the number of different statement types steadily increases. Instead a more traditional C++ technique of having a polymorphic base class `StmtData` to all of `PrintStmt`, `InitStmt` etc. is used, together with a shared pointer to a `data` member variable of this type within the `StmtNode` class itself. (Trying to use `StmtData` directly without the indirection through a pointer would lead to slicing.)

The forward-declaration of class `StmtNode` and its related types begins this header file.

```cpp
#ifndef Stmt_hpp
#define Stmt_hpp

#include "Expr.hpp"
#include "utility/Utility.hpp"
#include <boost/fusion/adapted/struct.hpp>
#include <memory>
#include <vector>
#include <variant>

namespace abaci::ast {

class StmtNode;

using StmtList = std::vector<StmtNode>;

using abaci::utility::Variable;
using abaci::utility::Operator;

struct StmtData {
    virtual ~StmtData() {}
};

```

This is followed by the class definition, with defaulted constructors (which are safe to use with a shared pointer member) and one taking a raw `StmtData*` pointer which it then takes ownership of. There is a "getter" for the single member `data`, again returning a raw pointer (for use by the code generation stage, which can be queried with `dynamic_cast`).

```cpp
class StmtNode {
public:
    StmtNode() = default;
    StmtNode(const StmtNode&) = default;
    StmtNode& operator=(const StmtNode&) = default;
    StmtNode(StmtData *nodeImpl) { data.reset(nodeImpl); }
    const StmtData *get() const { return data.get(); }
private:
    std::shared_ptr<StmtData> data;
};

```

Then come all of the AST containers for each different statement types; the types and orders of the data members for these match the exact sequence they appear in the X3 rules. Some of these members are not used, such as `CommentStmt::comment_string` and `ExprFunction::to`, but the majority are needed by the code generation functions in `codegen/StmtCodeGen.cpp`.

```cpp
struct CommentStmt : StmtData {
    std::string comment_string;
};

using PrintList = std::vector<std::variant<ExprNode,Operator>>;

struct PrintStmt : StmtData {
    ExprNode expression;
    PrintList format;
};

struct InitStmt : StmtData {
    Variable name;
    Operator assign;
    ExprNode value;
};

struct AssignStmt : StmtData {
    Variable name;
    Operator assign;
    ExprNode value;
};

struct IfStmt : StmtData {
    ExprNode condition;
    StmtList true_test;
    StmtList false_test;
};

struct WhileStmt : StmtData {
    ExprNode condition;
    StmtList loop_block;
};

struct RepeatStmt : StmtData {
    StmtList loop_block;
    ExprNode condition;
};

struct WhenStmt {
    ExprNode expression;
    StmtList block;
};

using WhenList = std::vector<WhenStmt>;

struct CaseStmt : StmtData {
    ExprNode case_value;
    WhenList matches;
    StmtList unmatched;
};

struct Function : StmtData {
    std::string name;
    std::vector<Variable> parameters;
    StmtList function_body;
};

struct FunctionCall : StmtData {
    std::string name;
    ExprList args;
};

struct ReturnStmt : StmtData {
    ExprNode expression;
    mutable int depth{ -1 };
};

struct ExprFunction : StmtData {
    std::string name;
    std::vector<Variable> parameters;
    Operator to;
    ExprNode expression;
};

using FunctionList = std::vector<Function>;

struct Class : StmtData {
    std::string name;
    std::vector<Variable> variables;
    FunctionList methods;    
};

}

```

At global scope, the AST structs must be adapted to the Boost Fusion library for ease of use with the X3 rules.

```cpp
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::CommentStmt, comment_string)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::PrintStmt, expression, format)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::InitStmt, name, assign, value)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::AssignStmt, name, assign, value)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::IfStmt, condition, true_test, false_test)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::WhileStmt, condition, loop_block)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::RepeatStmt, loop_block, condition)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::WhenStmt, expression, block)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::CaseStmt, case_value, matches, unmatched)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::Function, name, parameters, function_body)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::FunctionCall, name, args)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::ReturnStmt, expression)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::ExprFunction, name, parameters, to, expression)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::Class, name, variables, methods)

#endif
```

## Directory `parser`

### `Parse.hpp`

The interface of the parsing functionality is very simple, just three functions, all of which return `bool` (where success is indicated by `true`). It also has only `ast/Stmt.hpp` as a prerequisite, meaning that it does not add much compilation time to client code, and that the implementation file `parser/Parse.cpp` can be modified at will without affecting client code, speeding up the development cycle.

```cpp
#ifndef Parse_hpp
#define Parse_hpp

#include "ast/Stmt.hpp"
#include <string>

namespace abaci::parser {

```

Function `parse_block()` takes a multi-line string (which it does not modify) as input and constructs a `StmtList` in the second (reference) parameter.

```cpp
bool parse_block(const std::string& block_str, abaci::ast::StmtList& ast);

```

Function `parse_statement()` takes a string (possibly consisting of more than one statement) and constructs a single `StmtNode` in the second (again reference) parameter, modifying the string so it no longer contains the successfully parsed statement (ready for being called again).

```cpp
bool parse_statement(std::string& stmt_str, abaci::ast::StmtNode& ast);

```

Finally, the function `test_statement()` does not construct an AST, but merely performs a syntax analysis of the input string (which it does not modify), returning `true` for a valid parse (of a single statement, not allowing trailing input or multiple statements).

```cpp
bool test_statement(const std::string& stmt_str);

}

#endif
```

### `Parse.cpp`

This implementation file is the only part of the compiler which directly utilizes the X3 library, specifically header `<boost/spirit/home/x3.hpp>`. To describe X3 (very) briefly, it allows rule-based analysis and conversion of textual input, where complex rules are synthesised from elementary ones. This sounds a lot like the way parser-generators such as `bison` operate, and indeed there are similarities. However with X3 full control of the textual input is maintained, while parser-generators require a scanner function to return numeric tokens for matched strings. It is claimed that the parsing performance of X3 is close to that achieved with a hand-written parser, and there is no intermediate C++ file, the X3 rule definitions compile straight to object code.

The "compilation pipeline" is then modified from the usual "Lexer -> Parser -> Analysis -> Code Generation", to simply "Lexical Analysis -> Code Generation". Importantly, there is no need for a symbol table to be active during the parsing phase.

Writing an X3 parser involves a considerable amount of boilerplate code, but the rule definitions themselves are described using a terse syntax that resembles that for regular expressions (with necessary allowances for conventional C++ operator syntax). For a more complete introduction and a quick start, consult the X3 documentation; listed below are some of the most basic X3 constructs to allow the reader to gain some understanding of the source code for this file.

|      Regex      |      X3 Syntax     |     Description    |
|:---------------:|:------------------:|:------------------:|
|      ab         |        a >> b      | a followed by b    |
|      a*         |        *a          | zero or more a's   |
|      a+         |        +a          | one or more a's    |
|      a\|b       |       a\|b         | an a or b          |
|      a?         |        -a          | zero or one a's    |
|     a(ab+)?     |   a >> -(a >> +b)  | more complex       |

The file `parser/Parse.cpp` is structured in the following way:

1. `#include`s and `using` declarations
2. X3 rule declarations
3. Parsing helper functions (mostly lambdas of type `[](auto& ctx)`)
4. Function objects `MakeNode` and `MakeStmt`
5. X3 rule definitions
6. Invocations of `BOOST_SPIRIT_DEFINE()`
7. Definitions of the functions from `parser/Parse.hpp`

Most of the contents of namespaces `abaci::utility` and `abaci::ast` are used by this implementation file, however individual `using` declarations (rather than `using namespace`) have been used.

```cpp
#include "Parse.hpp"
#include "Keywords.hpp"
#include "utility/Utility.hpp"
#include "utility/Report.hpp"
#include "ast/Expr.hpp"
#include "ast/Stmt.hpp"
#include <boost/spirit/home/x3.hpp>
#include <charconv>

namespace abaci::parser {

namespace x3 = boost::spirit::x3;
using abaci::utility::AbaciValue;
using abaci::utility::Complex;
using abaci::utility::Operators;
using abaci::utility::Operator;
using abaci::utility::String;
using abaci::utility::Variable;

using x3::char_;
using x3::string;
using x3::lit;
using x3::lexeme;
using x3::digit;
using x3::xdigit;
using x3::alpha;

using abaci::ast::ExprNode;
using abaci::ast::ExprList;
using abaci::ast::ValueCall;
using abaci::ast::StmtNode;
using abaci::ast::StmtList;
using abaci::ast::CommentStmt;
using abaci::ast::PrintStmt;
using abaci::ast::PrintList;
using abaci::ast::InitStmt;
using abaci::ast::AssignStmt;
using abaci::ast::IfStmt;
using abaci::ast::WhileStmt;
using abaci::ast::RepeatStmt;
using abaci::ast::CaseStmt;
using abaci::ast::WhenStmt;
using abaci::ast::WhenList;
using abaci::ast::Function;
using abaci::ast::FunctionCall;
using abaci::ast::ReturnStmt;
using abaci::ast::ExprFunction;
using abaci::ast::Class;

```

X3 rule declarations take two template parameters, the class type and the semantic type for the rule. The declaration rule `rule_1` must have a corresponding definition `rule_1_def` later in the implementation file in order to use it with `BOOST_SPIRIT_DEFINE()`. The rules related to expressions are listed first.

```cpp
x3::rule<class number_str, std::string> number_str;
x3::rule<class base_number_str, std::string> base_number_str;
x3::rule<class boolean_str, std::string> boolean_str;
x3::rule<class string_str, std::string> string_str;
x3::rule<class value, AbaciValue> value;
x3::rule<class plus, Operator> const plus;
x3::rule<class minus, Operator> const minus;
x3::rule<class times, Operator> const times;
x3::rule<class divide, Operator> const divide;
x3::rule<class modulo, Operator> const modulo;
x3::rule<class floor_divide, Operator> const floor_divide;
x3::rule<class equal, Operator> const equal;
x3::rule<class not_equal, Operator> const not_equal;
x3::rule<class less, Operator> const less;
x3::rule<class less_equal, Operator> const less_equal;
x3::rule<class greater_equal, Operator> const greater_equal;
x3::rule<class greater, Operator> const greater;
x3::rule<class exponent, Operator> const exponent;
x3::rule<class logical_and, Operator> const logical_and;
x3::rule<class logical_or, Operator> const logical_or;
x3::rule<class logical_not, Operator> const logical_not;
x3::rule<class bitwise_and, Operator> const bitwise_and;
x3::rule<class bitwise_or, Operator> const bitwise_or;
x3::rule<class bitwise_xor, Operator> const bitwise_xor;
x3::rule<class bitwise_compl, Operator> const bitwise_compl;
x3::rule<class expression, ExprNode> const expression;
x3::rule<class logic_or, ExprList> const logic_or;
x3::rule<class logic_and_n, ExprNode> const logic_and_n;
x3::rule<class logic_and, ExprList> const logic_and;
x3::rule<class bit_or_n, ExprNode> const bit_or_n;
x3::rule<class bit_or, ExprList> const bit_or;
x3::rule<class bit_xor_n, ExprNode> const bit_xor_n;
x3::rule<class bit_xor, ExprList> const bit_xor;
x3::rule<class bit_and_n, ExprNode> const bit_and_n;
x3::rule<class bit_and, ExprList> const bit_and;
x3::rule<class comma, Operator> const comma;
x3::rule<class semicolon, Operator> const semicolon;
x3::rule<class assign, Operator> const assign;
x3::rule<class to, Operator> const to;
x3::rule<class equality_n, ExprNode> const equality_n;
x3::rule<class equality, ExprList> const equality;
x3::rule<class comaprison_n, ExprNode> const comparison_n;
x3::rule<class comparison, ExprList> const comparison;
x3::rule<class term_n, ExprNode> const term_n;
x3::rule<class term, ExprList> const term;
x3::rule<class factor_n, ExprNode> const factor_n;
x3::rule<class factor, ExprList> const factor;
x3::rule<class unary_n, ExprNode> const unary_n;
x3::rule<class unary, ExprList> const unary;
x3::rule<class index_n, ExprNode> const index_n;
x3::rule<class index, ExprList> const index;
x3::rule<class primary_n, ExprNode> const primary_n;
x3::rule<class identifier, std::string> const identifier;
x3::rule<class variable, Variable> const variable;
x3::rule<class function_value_call, ValueCall> const function_value_call;

```

Then come the rules related to statements.

```cpp
x3::rule<class comment_items, CommentStmt> const comment_items;
x3::rule<class comment, StmtNode> const comment;
x3::rule<class print_items, PrintStmt> const print_items;
x3::rule<class print_stmt, StmtNode> const print_stmt;
x3::rule<class let_items, InitStmt> const let_items;
x3::rule<class let_stmt, StmtNode> const let_stmt;
x3::rule<class assign_items, AssignStmt> const assign_items;
x3::rule<class assign_stmt, StmtNode> const assign_stmt;
x3::rule<class if_items, IfStmt> const if_items;
x3::rule<class if_stmt, StmtNode> const if_stmt;
x3::rule<class while_items, WhileStmt> const while_items;
x3::rule<class while_stmt, StmtNode> const while_stmt;
x3::rule<class repeat_items, RepeatStmt> const repeat_items;
x3::rule<class repeat_stmt, StmtNode> const repeat_stmt;
x3::rule<class when_items, WhenStmt> const when_items;
x3::rule<class case_items, CaseStmt> const case_items;
x3::rule<class case_stmt, StmtNode> const case_stmt;
x3::rule<class function_parameters, std::vector<Variable>> const function_parameters;
x3::rule<class function_items, Function> const function_items;
x3::rule<class function, StmtNode> const function;
x3::rule<class expression_function_items, ExprFunction> const expression_function_items;
x3::rule<class expression_function, StmtNode> const expression_function;
x3::rule<class call_args, ExprList> const call_args;
x3::rule<class call_items, FunctionCall> const call_items;
x3::rule<class function_call, StmtNode> const function_call;
x3::rule<class return_items, ReturnStmt> const return_items;
x3::rule<class return_stmt, StmtNode> const return_stmt;
x3::rule<class class_items, Class> const class_items;
x3::rule<class class_template, StmtNode> const class_template;
x3::rule<class keywords, std::string> const keywords;
x3::rule<class statment, StmtNode> const statement;
x3::rule<class block, StmtList> const block;

```

The rule helper functions are used to create a rule value (of a different type) from a (single) attribute, this follows the pattern of synthesised attributes. The attribute is available as `_attr(ctx)` while the rule's value can be set as `_val(ctx)`; a rule which uses such a function has the syntax `rule_name[helper_function]`.

Function `makeNumber` constructs an `AbaciValue()` of type `Complex`, `Floating` or `Integer` from a suitable string.

```cpp
auto makeNumber = [](auto& ctx) {
    const auto& str = _attr(ctx);
    if ((str.find(IMAGINARY1) != std::string::npos) || (str.find(IMAGINARY2) != std::string::npos)) {
        double f;
        std::from_chars(str.data(), str.data() + str.size() - 1, f);
        _val(ctx) = AbaciValue(0, f);
    }
    else if (str.find(DOT) != std::string::npos) {
        double d;
        std::from_chars(str.data(), str.data() + str.size(), d);
        _val(ctx) = AbaciValue(d);
    }
    else {
        unsigned long long ull;
        std::from_chars(str.data(), str.data() + str.size(), ull);
        _val(ctx) = AbaciValue(ull);
    }
};

```

Function `makeBaseNumber` constructs an `Integer` from a prefixed string.

```cpp
auto makeBaseNumber = [](auto& ctx) {
    const auto& str = _attr(ctx);
    unsigned long long ull{ 0 };
    if (str.find(HEX_PREFIX) == 0) {
        std::from_chars(str.data() + std::string(HEX_PREFIX).size(), str.data() + str.size(), ull, 16);
    }
    else if (str.find(BIN_PREFIX) == 0) {
        std::from_chars(str.data() + std::string(BIN_PREFIX).size(), str.data() + str.size(), ull, 2);
    }
    else if (str.find(OCT_PREFIX) == 0) {
        std::from_chars(str.data() + std::string(OCT_PREFIX).size(), str.data() + str.size(), ull, 8);
    }
    _val(ctx) = AbaciValue(ull);
};

```

Function `makeBoolean` constructs a `Boolean` or `Nil` from a string containing `true`, `false` or `nil`.

```cpp
auto makeBoolean = [](auto& ctx) {
    const auto& str = _attr(ctx);
    _val(ctx) = (str == NIL) ? AbaciValue() : AbaciValue((str == TRUE) ? true : false);
};

```

Functions `makeString` and `makeVariable` construct a `String` and `Variable` from a string.

```cpp
auto makeString = [](auto& ctx) {
    const auto& str = _attr(ctx);
    _val(ctx) = AbaciValue(str);
};

auto makeVariable = [](auto& ctx){
    const std::string str = _attr(ctx);
    _val(ctx) = Variable(str);
};

```

Function object `MakeNode` has a class template parameter being the `ExprNode::Type` field related to the `ExprList` attribute, which is fixed at compile-time by rule being evaluated. This is passed as the second parameter to the constructor. The `operator()` is templated on type `Context` in a similar way as for the lambdas previously.

```cpp
template<size_t Ty = ExprNode::Unset>
struct MakeNode {
    template<typename Context>
    void operator()(Context& ctx) {
        _val(ctx) = ExprNode(_attr(ctx), static_cast<ExprNode::Association>(Ty));
    }
};

```

Function object `MakeStmt` has a class template parameter being the type of the statement being created (`PrintStmt`, `ReturnStmt` etc.) which it uses to construct a `StmtNode`. Typically this involves packaging a rule's `statement_items` (of semantic type `...Stmt`) into `statement` of semantic type `StmtNode`. (Incorrect rule definitions is prone to causing very long error messages.)

```cpp
template<typename T>
struct MakeStmt {
    template<typename Context>
    void operator()(Context& ctx) {
        _val(ctx) = StmtNode(new T(std::move(_attr(ctx))));
    }
};

```

Function `getOperator` is necessary due to an unfortunate "feature" of X3 where textual sequences such as `string(">=")` are decomposed into individual `char` under some circumstances. This results in an `ExprList` holding `[AbaciValue,char,char,AbaciValue]` instead of the expected `[AbaciValue,std::string,AbaciValue]`. Relying on a rule to convert an `Operator`'s string representation into its token form solves this problem, similarly for `Variable`.

```cpp
auto getOperator = [](auto& ctx){
    const std::string str = _attr(ctx);
    const auto iter = Operators.find(str);
    if (iter != Operators.end()) {
        _val(ctx) = iter->second;
    }
    else {
        UnexpectedError("Unknown operator");
    }
};

```

The rules for different number inputs come next; use of `lexeme[...]` flattens the input to a single string.

```cpp
const auto number_str_def = lexeme[+digit >> -( string(DOT) >> +digit ) >> -( string(IMAGINARY1) | IMAGINARY2 )];
const auto base_number_str_def = lexeme[string(HEX_PREFIX) >> +xdigit]
    | lexeme[string(BIN_PREFIX) >> +char_('0', '1')]
    | lexeme[string(OCT_PREFIX) >> +char_('0', '7')];
const auto boolean_str_def = string(NIL) | string(FALSE) | string(TRUE);
const auto string_str_def = lexeme['"' >> *(char_ - '"') >> '"'];
const auto value_def = base_number_str[makeBaseNumber] | number_str[makeNumber] | boolean_str[makeBoolean] | string_str[makeString];

```

Then all of the `Operator` tokenization rule definitions.

```cpp
const auto plus_def = string(PLUS)[getOperator];
const auto minus_def = string(MINUS)[getOperator];
const auto times_def = string(TIMES)[getOperator];
const auto divide_def = string(DIVIDE)[getOperator];
const auto modulo_def = string(MODULO)[getOperator];
const auto floor_divide_def = string(FLOOR_DIVIDE)[getOperator];
const auto exponent_def = string(EXPONENT)[getOperator];

const auto equal_def = string(EQUAL)[getOperator];
const auto not_equal_def = string(NOT_EQUAL)[getOperator];
const auto less_def = string(LESS)[getOperator];
const auto less_equal_def = string(LESS_EQUAL)[getOperator];
const auto greater_equal_def = string(GREATER_EQUAL)[getOperator];
const auto greater_def = string(GREATER)[getOperator];

const auto logical_and_def = string(AND)[getOperator];
const auto logical_or_def = string(OR)[getOperator];
const auto logical_not_def = string(NOT)[getOperator];
const auto bitwise_and_def = string(BITWISE_AND)[getOperator];
const auto bitwise_or_def = string(BITWISE_OR)[getOperator];
const auto bitwise_xor_def = string(BITWISE_XOR)[getOperator];
const auto bitwise_compl_def = string(BITWISE_COMPL)[getOperator];

const auto comma_def = string(COMMA)[getOperator];
const auto semicolon_def = string(SEMICOLON)[getOperator];
const auto assign_def = string(ASSIGN)[getOperator];
const auto to_def = string(TO)[getOperator];

```

Rules for identifiers as variables and value call components come next.

```cpp
const auto identifier_def = lexeme[( ( alpha | '\'' | char_('\200', '\377') ) >> *( alpha | digit | '_' | '\'' | char_('\200', '\377') ) ) - keywords];
const auto variable_def = identifier[makeVariable];
const auto function_value_call_def = identifier >> call_args;

```

The rules for expressions come in pairs, for example `logic_and_n_def` and `logic_and_def` (the `_n` stands for "node"). They are listed in order of precedence from low to high, note that `primary_n_def` calls `logic_or[MakeNode<ExprNode::Boolean>()]` directly and not `expression` for its (recursive) parentheses evaluation.

```cpp
const auto expression_def = logic_or[MakeNode<ExprNode::Boolean>()];
const auto logic_or_def = logic_and_n >> *( logical_or >> logic_and_n );
const auto logic_and_n_def = logic_and[MakeNode<ExprNode::Boolean>()];
const auto logic_and_def = bit_or_n >> *( logical_and >> bit_or_n );
const auto bit_or_n_def = bit_or[MakeNode<ExprNode::Left>()];
const auto bit_or_def = bit_xor_n >> *( bitwise_or >> bit_xor_n );
const auto bit_xor_n_def = bit_xor[MakeNode<ExprNode::Left>()];
const auto bit_xor_def = bit_and_n >> *( bitwise_xor >> bit_and_n );
const auto bit_and_n_def = bit_and[MakeNode<ExprNode::Left>()];
const auto bit_and_def = equality_n >> *( bitwise_and >> equality_n );
const auto equality_n_def = equality[MakeNode<ExprNode::Boolean>()];
const auto equality_def = comparison_n >> *( (equal | not_equal) >> comparison_n );
const auto comparison_n_def = comparison[MakeNode<ExprNode::Boolean>()];
const auto comparison_def = term_n >> -( ( greater_equal | greater ) >> term_n
    | ( less_equal | less ) >> term_n >> -( ( less_equal | less ) >> term_n ) );
const auto term_n_def = term[MakeNode<ExprNode::Left>()];
const auto term_def = factor_n >> *( ( plus | minus ) >> factor_n );
const auto factor_n_def = factor[MakeNode<ExprNode::Left>()];
const auto factor_def = unary_n >> *( ( times | floor_divide | divide | modulo ) >> unary_n);
const auto unary_n_def = unary[MakeNode<ExprNode::Unary>()];
const auto unary_def = *( minus | logical_not | bitwise_compl ) >> index_n;
const auto index_n_def = index[MakeNode<ExprNode::Right>()];
const auto index_def = primary_n >> *( exponent >> unary_n );
const auto primary_n_def = value[MakeNode<>()] | LEFT_PAREN >> logic_or[MakeNode<ExprNode::Boolean>()] >> RIGHT_PAREN | function_value_call[MakeNode<>()] | variable[MakeNode<>()];

```

The keywords are listed in order to disallow them as identifier names, note the first `lit(AND)` allows use of `|` with subsequent `constexpr char *` items.

```cpp
const auto keywords_def = lit(AND) | CASE | CLASS | ELSE | ENDCASE | ENDCLASS | ENDFN | ENDIF | ENDWHILE
    | FALSE | FN | IF | LET | NIL | NOT | OR | PRINT | RETURN | TRUE | WHEN | WHILE;

```

Next are the statement rules themselves, which are what gives the reader (and parser function) an idea of the syntax of the Abaci0 language. All of the different values for rule definition `statement_def` are of type `StmtNode`, and hence all must make use of `MakeStmt<...Stmt>()` in order to appear to be the same type.

Keyword `rem` matches the rest of the line and then ignores it, thus simulating a comment as found in other languages. It is the only keyword which is not entirely whitespace neutral. The syntax `char_ - '\n'` is the set difference operator in use (all characters except newline).

```cpp
const auto comment_items_def = *(char_ - '\n');
const auto comment_def = REM >> comment_items[MakeStmt<CommentStmt>()];
```

Keyword `print` allows output of a single value, followed by optional padding (caused by use of `,`, affecting the next `print` statement) or a newline (suppressed with `;`). It was intended to allow multiple comma-separated values to be output, and the parser and backend were found to be capable of this; however the code fragment `let x <- 10 print x, x <- x + 1` outputted `10 false` due to evaluating `x, x < (-x + 1)` (parentheses added for clarity).

```cpp
const auto print_items_def = expression >> -( +comma | semicolon );
const auto print_stmt_def = PRINT >> print_items[MakeStmt<PrintStmt>()];
```

Keyword `let` is followed by either `=` (to initialize a constant) or `<-` (to initialize a variable).

```cpp
const auto let_items_def = variable >> (equal | assign) >> expression;
const auto let_stmt_def = LET >> let_items[MakeStmt<InitStmt>()];
```

Reassignments do no use the `let` keyword.

```cpp
const auto assign_items_def = variable >> assign >> expression;
const auto assign_stmt_def = assign_items[MakeStmt<AssignStmt>()];

```

Keyword `if` begins a lexical sub-scope and allows an optional `else` clause before `endif`.

```cpp
const auto if_items_def = expression >> block >> -( ELSE >> block );
const auto if_stmt_def = IF >> if_items[MakeStmt<IfStmt>()] >> ENDIF;

```

Keyword `while` begins a pre-condition loop and lexical sub-scope up to `endwhile`.

```cpp
const auto while_items_def = expression >> block;
const auto while_stmt_def = WHILE >> while_items[MakeStmt<WhileStmt>()] >> ENDWHILE;
```

Keyword `repeat` begins a post-condition loop and lexical sub-scope up to `until`.

```cpp
const auto repeat_items_def = block >> UNTIL >> expression;
const auto repeat_stmt_def = REPEAT >> repeat_items[MakeStmt<RepeatStmt>()];

```

Keyword `case` begins a complex construct requiring several lexical sub-scopes (one for each `when` and one for the optional `else`) and ends with `endcase`.

```cpp
const auto when_items_def = WHEN >> expression >> block;
const auto case_items_def = expression >> *when_items >> -( ELSE >> block );
const auto case_stmt_def = CASE >> case_items[MakeStmt<CaseStmt>()] >> ENDCASE;

```

Function definitions of the form `fn f() print "Abaci0" endif` and `let f'(n) -> n + 1` can be entered.

```cpp
const auto function_parameters_def = LEFT_PAREN >> -( variable >> *( COMMA >> variable ) ) >> RIGHT_PAREN;
const auto function_items_def = identifier >> function_parameters >> block;
const auto function_def = FN >> function_items[MakeStmt<Function>()] >> ENDFN;
const auto expression_function_items_def = identifier >> function_parameters >> to >> expression;
const auto expression_function_def = LET >> expression_function_items[MakeStmt<ExprFunction>()];

```

Function calls are distinct statements compared to expressions involving function call(s).

```cpp
const auto call_args_def = LEFT_PAREN >> -( expression >> *( COMMA >> expression) ) >> RIGHT_PAREN;
const auto call_items_def = identifier >> call_args;
const auto function_call_def = call_items[MakeStmt<FunctionCall>()];
```

Keyword `return` allows a function to return a single value.

```cpp
const auto return_items_def = expression;
const auto return_stmt_def = RETURN >> return_items[MakeStmt<ReturnStmt>()];

```

Keyword `class` allows class definitions with member variables and optional member functions to be entered. [Note: Further use of classes is not currently implemented.]

```cpp
const auto class_items_def = identifier >> function_parameters >> *(FN >> function_items >> ENDFN);
const auto class_template_def = CLASS >> class_items[MakeStmt<Class>()] >> ENDCLASS;

```

The order of the rules for `statement_def` is significant in some cases because greedy matching does not (and cannot) take place, the first (partially) matching rule always wins.

```cpp
const auto statement_def = if_stmt | print_stmt | expression_function | let_stmt | assign_stmt | while_stmt | repeat_stmt | case_stmt | function | function_call | return_stmt | class_template | comment; 
```

The power of X3 is demonstrated by `block_def` as being simply a list of zero or more `statement`s.

```cpp
const auto block_def = *statement;

```

The calls to `BOOST_SPIRIT_DEFINE()` are grouped in the same ways as for the rule definitions, but this grouping is not significant.

```cpp
BOOST_SPIRIT_DEFINE(number_str, base_number_str, boolean_str, string_str, value)
BOOST_SPIRIT_DEFINE(plus, minus, times, divide, modulo, floor_divide, exponent,
    equal, not_equal, less, less_equal, greater_equal, greater,
    logical_and, logical_or, logical_not, bitwise_and, bitwise_or, bitwise_xor, bitwise_compl,
    comma, semicolon, assign, to)
BOOST_SPIRIT_DEFINE(expression, logic_or, logic_and, logic_and_n,
    bit_or, bit_or_n, bit_xor, bit_xor_n, bit_and, bit_and_n,
    equality, equality_n, comparison, comparison_n,
    term, term_n, factor, factor_n, unary, unary_n, index, index_n, primary_n)
BOOST_SPIRIT_DEFINE(identifier, variable, function_value_call, keywords,
    comment_items, comment, print_items, print_stmt,
    let_items, let_stmt, assign_items, assign_stmt, if_items, if_stmt,
    when_items, while_items, while_stmt, repeat_items, repeat_stmt, case_items, case_stmt,
    function_parameters, function_items, function, call_args, call_items, function_call,
    expression_function_items, expression_function,
    return_items, return_stmt, class_items, class_template, statement, block)

```

The three functions declared in `parser/Parse.hpp` all call `phrase_parse()` with an iterator range, rule name, space skipper class and a reference to the target (intially empty) AST being constructed. It is `phrase_parse()`'s instantiation which really tests the template metaprogramming logic in the parser rule definitions, and can cause very long error messages to be output. Notice that variable `iter` is updated to point to the first unprocessed input character, so `parse_statement()` uses this fact to modify its parameter `stmt_str`.

```cpp
bool parse_block(const std::string& block_str, abaci::ast::StmtList& ast) {
    auto iter = block_str.begin();
    auto end = block_str.end();
    return phrase_parse(iter, end, abaci::parser::block, x3::ascii::space, ast);
}

bool parse_statement(std::string& stmt_str, abaci::ast::StmtNode& ast) {
    auto iter = stmt_str.begin();
    auto end = stmt_str.end();
    bool result = phrase_parse(iter, end, abaci::parser::statement, x3::ascii::space, ast);
    stmt_str = std::string(iter, end);
    return result;
}

bool test_statement(const std::string& stmt_str) {
    auto iter = stmt_str.begin();
    auto end = stmt_str.end();
    return phrase_parse(iter, end, abaci::parser::statement, x3::ascii::space);
}

}
```

### `Keywords.hpp`

This header file specifies all of the Abaci0 keywords and symbolic tokens. In order to translate Abaci0 into a non-English language, or provide single (UTF-8) symbols for sequences such as ">=", modifing this file should be all that is required. (Error messages and other diagnostics would still be in English as they are currently hard-coded into the source files.)

```cpp
#ifndef Keywords_hpp
#define Keywords_hpp

constexpr auto *AND = "and";
constexpr auto *CASE = "case";
constexpr auto *CLASS = "class";
constexpr auto *ELSE = "else";
constexpr auto *ENDCASE = "endcase";
constexpr auto *ENDCLASS = "endclass";
constexpr auto *ENDFN = "endfn";
constexpr auto *ENDIF = "endif";
constexpr auto *ENDWHILE = "endwhile";
constexpr auto *FALSE = "false";
constexpr auto *FN = "fn";
constexpr auto *IF = "if";
constexpr auto *LET = "let";
constexpr auto *NIL = "nil";
constexpr auto *NOT = "not";
constexpr auto *OR = "or";
constexpr auto *PRINT = "print";
constexpr auto *REM = "rem";
constexpr auto *REPEAT = "repeat";
constexpr auto *RETURN = "return";
constexpr auto *TRUE = "true";
constexpr auto *UNTIL = "until";
constexpr auto *WHEN = "when";
constexpr auto *WHILE = "while";

constexpr auto *PLUS = "+";
constexpr auto *MINUS = "-";
constexpr auto *TIMES = "*";
constexpr auto *DIVIDE = "/";
constexpr auto *MODULO = "%";
constexpr auto *FLOOR_DIVIDE = "//";
constexpr auto *EXPONENT = "**";

constexpr auto *EQUAL = "=";
constexpr auto *NOT_EQUAL = "/=";
constexpr auto *LESS = "<";
constexpr auto *LESS_EQUAL = "<=";
constexpr auto *GREATER_EQUAL = ">=";
constexpr auto *GREATER = ">";

constexpr auto *BITWISE_AND = "&";
constexpr auto *BITWISE_OR = "|";
constexpr auto *BITWISE_XOR = "^";
constexpr auto *BITWISE_COMPL = "~";

constexpr auto *COMMA = ",";
constexpr auto *DOT = ".";
constexpr auto *SEMICOLON = ";";
constexpr auto *COLON = ":";
constexpr auto *LEFT_PAREN = "(";
constexpr auto *RIGHT_PAREN = ")";
constexpr auto *ASSIGN = "<-";
constexpr auto *TO = "->";
constexpr auto *IMAGINARY1 = "j";
constexpr auto *IMAGINARY2 = "J";
constexpr auto *HEX_PREFIX = "0x";
constexpr auto *OCT_PREFIX = "0";
constexpr auto *BIN_PREFIX = "0b";

#endif
```

## Directory `codegen`

The implementation files `codegen/ExprCodeGen.cpp` and `codegen/StmtCodeGen.cpp` contain almost all of the code for translating the constructed Abstract Syntax Tree into LLVM API calls. By necessity they are quite lengthy (~700 and ~400 lines) and in particular class `ExprCodeGen` contains a sizeable `operator()` member function which handles code generation of arbitrarily complex `ExprNode` trees (and calls itself recursively at numerous points). Class `StmtCodeGen` parses both single statements and blocks, calling a generic function `codeGen()` which is specialized in the implementation file to the statement types in `ast/Stmt.hpp`.

Implementation file `codegen/TypeCodeGen.cpp` exists to cover up a hole in the static type system, which is that functions returning a value must be partially evaluated in order to determine this return type. The return type may depend upon parameter type(s) and global variables, and therefore a complete parse of the function, including all the lexical sub-scopes, is performed by the two classes `TypeEvalGen` and `TypeCodeGen`.

### `CodeGen.hpp`

Class `ExprCodeGen` is initialized from a `JIT` instance, from which it obtains references to `llvm::IRBuilder<>` and `llvm::Module`. Its purpose is to reduce an `ExprNode` tree to a single `StackType`, consisting of an `llvm::Value*` (always an LLVM register) and `AbaciValue::Type` (known at compile-time to due to the static type system). The stack itself is simply a `std::vector<StackType>` qualified with `mutable` so that `operator()` can be declared `const`, and private functions `pop()` and `push()` operate on this stack. After a (single) call to `operator()`, the expected size of the stack is exactly 1, and the contents of this are returned by the `get()` member function. The member function `toBoolean()` creates a truth value for its input (as an `llvm::Value*`), while `promote()` ensures that numeric types are promoted so that the various Math operations they can be applied to entities of the same type.

```cpp
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

```

Statements and blocks are converted into code by class `StmtCodeGen`, again initialized from a reference to a `JIT`, together with an optional `llvm::BasicBlock*` and nesting depth, used only by the evaluation of an Abaci0 `return` statement when functions are being instantiated. The two overloads of `operator()` are called recursively by each other at numerous points in the implementation file, and generic declaration `codeGen()` is explicitly specialized (with no generic definition).

```cpp
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

```

The final two classes are initialized from an `Environment*` and a `Cache*` (from `engine/Cache.hpp`). In order to return a type for a particular function, class `TypeEvalGen` maintains a stack, however it is only concerned with the `AbaciValue::Type`. Functions `pop()` and `push()` manage this stack and `get()` expects a size of exactly 1. Functions `operator()` and `promote()` are present, giving some further degree of similarity to class `ExprCodeGen`. Class `TypeCodeGen` has only two more data members (other than the constructor parameters), being a `type_is_set` flag and and `AbaciValue::Type`. These are only set by the specialization of `codeGen()` for an Abaci0 `return` statment, and are queried from client code with method `get()`.

```cpp
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
```

### `ExprCodeGen.cpp`

The whole of the `llvm` namespace is made available to this implementation file, together with four other types. The large `operator()` function is based around switching upon the type of the `std::variant` in the `ExprNode` parameter, and in the case of it being an `ExprList`, its arithmetic association.

```cpp
#include "CodeGen.hpp"
#include "engine/JIT.hpp"
#include "utility/Utility.hpp"
#include <llvm/IR/Constants.h>
#include <algorithm>
#include <cstring>

using namespace llvm;

namespace abaci::codegen {

using abaci::ast::ExprNode;
using abaci::ast::ExprList;
using abaci::ast::ValueCall;
using abaci::utility::Operator;

void ExprCodeGen::operator()(const abaci::ast::ExprNode& node) const {
    switch (node.get().index()) {
```

For an `ValueNode` the value is extracted from the data in the AST and pushed to the stack, using `builder.CreateAlloca()` in the case of `Complex` and `String` to ensure no memory is leaked.

```cpp
        case ExprNode::ValueNode: {
            auto value = std::get<AbaciValue>(node.get());
            switch (value.type) {
                case AbaciValue::Nil:
                    push({ ConstantInt::get(builder.getInt8Ty(), 0), value.type });
                    break;
                case AbaciValue::Boolean:
                    push({ ConstantInt::get(builder.getInt1Ty(), value.value.boolean), value.type });
                    break;
                case AbaciValue::Integer:
                    push({ ConstantInt::get(builder.getInt64Ty(), value.value.integer), value.type });
                    break;
                case AbaciValue::Float:
                    push({ ConstantFP::get(builder.getDoubleTy(), value.value.floating), value.type });
                    break;
                case AbaciValue::Complex: {
                    auto complex_value = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                    auto real = ConstantFP::get(builder.getDoubleTy(), value.value.complex->real);
                    auto imag = ConstantFP::get(builder.getDoubleTy(), value.value.complex->imag);
                    auto complex_constant = ConstantStruct::get(jit.getNamedType("struct.Complex"), { real, imag });
                    builder.CreateStore(complex_constant, complex_value);
                    push({ complex_value, value.type });
                    break;
                }
                case AbaciValue::String: {
                    Constant *str = ConstantDataArray::getString(module.getContext(),
                        reinterpret_cast<char*>(value.value.str->ptr));
                    AllocaInst *ptr = builder.CreateAlloca(str->getType(), nullptr);
                    builder.CreateStore(str, ptr);
                    auto string_value = builder.CreateAlloca(jit.getNamedType("struct.String"));
                    auto len = ConstantInt::get(builder.getInt64Ty(), value.value.str->len);
                    builder.CreateStore(ptr, builder.CreateStructGEP(jit.getNamedType("struct.String"), string_value, 0));
                    builder.CreateStore(len, builder.CreateStructGEP(jit.getNamedType("struct.String"), string_value, 1));
                    push({ string_value, value.type });
                    break;
                }
                case AbaciValue::Object:
                    UnexpectedError("Cannot reassign objects.");
                default:
                    UnexpectedError("Not yet implemented");
            }
            break;
        }
```

In the case of an `VariableNode` the name is stored in the generated code before a call to library function `getVariable()`. The type is available from the current `DefineScope` and this is switched upon to cast the `value` field as necessary (no memory management is necessary as it remains in the symbol table, no copy is made.)

```cpp
        case ExprNode::VariableNode: {
            auto variable = std::get<Variable>(node.get());
            Constant *name = ConstantDataArray::getString(module.getContext(), variable.get().c_str());
            AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
            builder.CreateStore(name, str);
            Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
            Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
            auto abaci_value = builder.CreateCall(module.getFunction("getVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()) });
            Value *raw_value = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
            auto type = static_cast<AbaciValue::Type>(environment->getCurrentDefineScope()->getType(variable.get()) & AbaciValue::TypeMask);
            Value *value;
            switch (type) {
                case AbaciValue::Nil:
                case AbaciValue::Integer:
                    value = raw_value;
                    break;
                case AbaciValue::Boolean:
                    value = builder.CreateBitCast(raw_value, builder.getInt1Ty());
                    break;
                case AbaciValue::Float:
                    value = builder.CreateBitCast(raw_value, builder.getDoubleTy());
                    break;
                case AbaciValue::Complex:
                    value = builder.CreateBitCast(raw_value, PointerType::get(jit.getNamedType("struct.Complex"), 0));
                    break;
                case AbaciValue::String:
                    value = builder.CreateBitCast(raw_value, PointerType::get(jit.getNamedType("struct.String"), 0));
                    break;
                default:
                    UnexpectedError("Bad type for dereference");
            }
            push({ value, type });
            break;
        }
```

If a `CallNode` is found, things are slightly more complicated. Firstly it must be determined whether the symbol is a function call or class constructor. [Note: Class construction is currently not implemented.] For a function call, a full evaluation of the actual parameters (arguments) is necessary, before entering a sub-scope which stores the values returned as the names of the parameters from the (previously cached) function definition. The return type of the function is obtained with a call to getFunctionInstantiationType() and the type of the return variable (named `_return`) is set along with a zeroized value field. A call to the mangled name of the function (based on the types of the actual parameters) is followed by reading `_return` and pushing a deep copy onto the stack.

```cpp
        case ExprNode::CallNode: {
            const auto& call = std::get<ValueCall>(node.get());
            switch(jit.getCache()->getCacheType(call.name)) {
                case Cache::CacheFunction: {
                    const auto& cache_function = jit.getCache()->getFunction(call.name);
                    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(jit.getEnvironment()));
                    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
                    std::vector<StackType> arguments;
                    std::vector<AbaciValue::Type> types;
                    for (const auto& arg : call.args) {
                        ExprCodeGen expr(jit);
                        expr(arg);
                        arguments.push_back(expr.get());
                        types.push_back(static_cast<AbaciValue::Type>(arguments.back().second & AbaciValue::TypeMask));
                    }
                    environment->beginDefineScope();
                    builder.CreateCall(module.getFunction("beginScope"), { typed_environment_ptr });
                    for (auto arg = arguments.begin(); const auto& parameter : cache_function.parameters) {
                        Constant *name = ConstantDataArray::getString(module.getContext(), parameter.get().c_str());
                        AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
                        builder.CreateStore(name, str);
                        auto result = *arg++;
                        auto type = static_cast<AbaciValue::Type>(result.second | AbaciValue::Constant);
                        environment->getCurrentDefineScope()->setType(parameter.get(), type);
                        auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
                        builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
                        builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), type), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
                        Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(jit.getEnvironment()));
                        Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
                        builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), abaci_value, ConstantInt::get(builder.getInt1Ty(), true) });
                    }
                    auto type = jit.getCache()->getFunctionInstantiationType(call.name, types);
                    environment->getCurrentDefineScope()->setType("_return", type);
                    Constant *name = ConstantDataArray::getString(module.getContext(), "_return");
                    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
                    builder.CreateStore(name, str);
                    auto return_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
                    builder.CreateStore(ConstantInt::get(builder.getInt64Ty(), 0), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), return_value, 0));
                    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), type), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), return_value, 1));
                    builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), return_value, ConstantInt::get(builder.getInt1Ty(), true) });
                    std::string function_name{ mangled(call.name, types) };
                    builder.CreateCall(module.getFunction(function_name), {});
                    auto abaci_value = builder.CreateCall(module.getFunction("getVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()) });
                    Value *raw_value = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
                    Value *value;
                    switch (type) {
                        case AbaciValue::Nil:
                        case AbaciValue::Integer:
                            value = raw_value;
                            break;
                        case AbaciValue::Boolean:
                            value = builder.CreateBitCast(raw_value, builder.getInt1Ty());
                            break;
                        case AbaciValue::Float:
                            value = builder.CreateBitCast(raw_value, builder.getDoubleTy());
                            break;
                        case AbaciValue::Complex: {
                            raw_value = builder.CreateBitCast(raw_value, PointerType::get(jit.getNamedType("struct.Complex"), 0));
                            value = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                            Value *real = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), raw_value, 0));
                            Value *imag = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), raw_value, 1));
                            builder.CreateStore(real, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), value, 0));
                            builder.CreateStore(imag, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), value, 1));
                            break;
                        }
                        case AbaciValue::String: {
                            raw_value = builder.CreateBitCast(raw_value, PointerType::get(jit.getNamedType("struct.String"), 0));
                            value = builder.CreateAlloca(jit.getNamedType("struct.String"));
                            Value *str = builder.CreateLoad(builder.getInt8PtrTy(), builder.CreateStructGEP(jit.getNamedType("struct.String"), raw_value, 0));
                            Value *len = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.String"), raw_value, 1));
                            AllocaInst *ptr = builder.CreateAlloca(builder.getInt8Ty(), len);
                            builder.CreateMemCpy(ptr, MaybeAlign(1), str, MaybeAlign(1), len);
                            builder.CreateStore(ptr, builder.CreateStructGEP(jit.getNamedType("struct.String"), value, 0));
                            builder.CreateStore(len, builder.CreateStructGEP(jit.getNamedType("struct.String"), value, 1));
                            break;
                        }
                        default:
                            UnexpectedError("Bad type for dereference");
                    }
                    push({ value, type });
                    builder.CreateCall(module.getFunction("endScope"), { typed_environment_ptr });
                    environment->endDefineScope();
                    break;
                }
                case Cache::CacheClass: {
                    UnexpectedError("Instantiation of classes not yet implemented.");
                    break;
                }
                default:
                    UnexpectedError("Not a function or class.");
            }
            break;
        }
```

For an `ListNode` a further switch is made on the association type. For `Left` association the first element of the `ExprList` is evaluated on a call to `operator()` (via `(*this)(expr.front()`), and then the remaining `Operator`/`ExprNode` pairs are progressively acted upon to modify the variable `result`, making the types the same by a call to `promote()`. Firstly the obtained type is switched upon followed by switches upon the `Operator`. (In the case of `Operator::Divide`, two `Integer` values are always promoted to `Floating`, `Operator::FloorDivide` is also provided.) For type `AbaciValue::Complex` a call to library function `complexMath()` is needed, all of the other operations (including string concatenation) are provided by LLVM API call(s). The resulting value and type are pushed onto the stack.

```cpp
        case ExprNode::ListNode: {
            switch (node.getAssociation()) {
                case ExprNode::Left: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.front());
                    auto result = pop();
                    for (auto iter = ++expr.begin(); iter != expr.end();) {
                        auto op = std::get<Operator>(iter++->get());
                        (*this)(*iter++);
                        auto operand = pop();
                        auto type = (operand.second != result.second) ? promote(result, operand) : result.second;
                        switch (type) {
                            case AbaciValue::Boolean:
                                switch (op) {
                                    case Operator::BitAnd:
                                        result.first = builder.CreateAnd(result.first, operand.first);
                                        break;
                                    case Operator::BitXor:
                                        result.first = builder.CreateXor(result.first, operand.first);
                                        break;
                                    case Operator::BitOr:
                                        result.first = builder.CreateOr(result.first, operand.first);
                                        break;
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Integer:
                                switch (op) {
                                    case Operator::Plus:
                                        result.first = builder.CreateAdd(result.first, operand.first);
                                        break;
                                    case Operator::Minus:
                                        result.first = builder.CreateSub(result.first, operand.first);
                                        break;
                                    case Operator::Times:
                                        result.first = builder.CreateMul(result.first, operand.first);
                                        break;
                                    case Operator::Modulo:
                                        result.first = builder.CreateSRem(result.first, operand.first);
                                        break;
                                    case Operator::FloorDivide:
                                        result.first = builder.CreateSDiv(result.first, operand.first);
                                        break;
                                    case Operator::Divide: {
                                        auto a = builder.CreateSIToFP(result.first, Type::getDoubleTy(module.getContext()));
                                        auto b = builder.CreateSIToFP(operand.first, Type::getDoubleTy(module.getContext()));
                                        result.first = builder.CreateFDiv(a, b);
                                        result.second = AbaciValue::Float;
                                        break;
                                    }
                                    case Operator::BitAnd:
                                        result.first = builder.CreateAnd(result.first, operand.first);
                                        break;
                                    case Operator::BitXor:
                                        result.first = builder.CreateXor(result.first, operand.first);
                                        break;
                                    case Operator::BitOr:
                                        result.first = builder.CreateOr(result.first, operand.first);
                                        break;
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Float:
                                switch (op) {
                                    case Operator::Plus:
                                        result.first = builder.CreateFAdd(result.first, operand.first);
                                        break;
                                    case Operator::Minus:
                                        result.first = builder.CreateFSub(result.first, operand.first);
                                        break;
                                    case Operator::Times:
                                        result.first = builder.CreateFMul(result.first, operand.first);
                                        break;
                                    case Operator::Divide:
                                        result.first = builder.CreateFDiv(result.first, operand.first);
                                        break;
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Complex:
                                switch (op) {
                                    case Operator::Plus:
                                    case Operator::Minus:
                                    case Operator::Times:
                                    case Operator::Divide: {
                                        Function *complexMathFunc = module.getFunction("complexMath");
                                        auto cplx = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                                        builder.CreateCall(complexMathFunc,
                                            { cplx, ConstantInt::get(builder.getInt32Ty(), static_cast<int>(op)),
                                                result.first, operand.first });
                                        result.first = cplx;
                                        break;
                                    }
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::String:
                                switch (op) {
                                    case Operator::Plus: {
                                        auto str = builder.CreateAlloca(jit.getNamedType("struct.String"));
                                        Value *str1_len = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.String"), result.first, 1));
                                        Value *str2_len = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.String"), operand.first, 1));
                                        Value *total_len = builder.CreateAdd(builder.CreateAdd(str1_len, str2_len), ConstantInt::get(builder.getInt64Ty(), 1));
                                        AllocaInst *ptr = builder.CreateAlloca(builder.getInt8Ty(), total_len);
                                        Value *str1_ptr = builder.CreateLoad(builder.getInt8PtrTy(), builder.CreateStructGEP(jit.getNamedType("struct.String"), result.first, 0));
                                        Value *str2_ptr = builder.CreateLoad(builder.getInt8PtrTy(), builder.CreateStructGEP(jit.getNamedType("struct.String"), operand.first, 0));
                                        builder.CreateMemCpy(ptr, MaybeAlign(1), str1_ptr, MaybeAlign(1), str1_len);
                                        Value *ptr_concat = builder.CreateGEP(builder.getInt8Ty(), ptr, str1_len);
                                        builder.CreateMemCpy(ptr_concat, MaybeAlign(1), str2_ptr, MaybeAlign(1), str2_len);
                                        Value *ptr_concat_null = builder.CreateGEP(builder.getInt8Ty(), ptr_concat, str2_len);
                                        builder.CreateStore(ConstantInt::get(builder.getInt8Ty(), 0), ptr_concat_null);
                                        builder.CreateStore(ptr, builder.CreateStructGEP(jit.getNamedType("struct.String"), str, 0));
                                        builder.CreateStore(total_len, builder.CreateStructGEP(jit.getNamedType("struct.String"), str, 1));
                                        result.first = str;
                                        break;
                                    }
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            default:
                                UnexpectedError("Not yet implemented");
                                break;
                        }
                    }
                    push(result);
                    break;
                }
```

The only `Right` associative operator is `**` (exponentiation) and for this the `ExprList` is processed from the right-end. In the case of `Integer` values these are promoted to `Floating`.

```cpp
                case ExprNode::Right: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.back());
                    auto result = pop();
                    for (auto iter = ++expr.rbegin(); iter != expr.rend();) {
                        auto op = std::get<Operator>(iter++->get());
                        (*this)(*iter++);
                        auto operand = pop();
                        auto type = (operand.second != result.second) ? promote(result, operand) : result.second;
                        switch (type) {
                            case AbaciValue::Integer:
                                switch (op) {
                                    case Operator::Exponent: {
                                        auto a = builder.CreateSIToFP(result.first, Type::getDoubleTy(module.getContext()));
                                        auto b = builder.CreateSIToFP(operand.first, Type::getDoubleTy(module.getContext()));
                                        Function *powFunc = module.getFunction("pow");
                                        result.first = builder.CreateCall(powFunc, { b, a });
                                        result.second = AbaciValue::Float;
                                        break;
                                    }
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Float:
                                switch (op) {
                                    case Operator::Exponent: {
                                        Function *powFunc = module.getFunction("pow");
                                        result.first = builder.CreateCall(powFunc, { operand.first, result.first });
                                        break;
                                    }
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Complex:
                                switch (op) {
                                    case Operator::Exponent: {
                                        Function *complexMathFunc = module.getFunction("complexMath");
                                        auto cplx = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                                        builder.CreateCall(complexMathFunc,
                                            { cplx, ConstantInt::get(builder.getInt32Ty(), static_cast<int>(op)),
                                                operand.first, result.first });
                                        result.first = cplx;
                                        break;
                                    }
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                        default:
                            UnexpectedError("Not yet implemented");
                            break;
                        }
                    }
                    push(result);
                    break;
                }
```

The three `Unary` operators are handled by the next part, obtaining the value at the right-end and progressively applying operators `not`, `~` and `-` from the "inside out".

```cpp
                case ExprNode::Unary: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.back());
                    auto result = pop();
                    auto type = result.second;
                    for (auto iter = ++expr.rbegin(); iter != expr.rend();) {
                        auto op = std::get<Operator>(iter++->get());
                        switch (type) {
                            case AbaciValue::Boolean:
                                switch (op) {
                                    case Operator::Not:
                                    case Operator::Compl:
                                        result.first = builder.CreateNot(result.first);
                                        break;
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Integer:
                                switch (op) {
                                    case Operator::Minus:
                                        result.first = builder.CreateNeg(result.first);
                                        break;
                                    case Operator::Not:
                                        toBoolean(result);
                                        result.first = builder.CreateNot(toBoolean(result));
                                        result.second = AbaciValue::Boolean;
                                        break;
                                    case Operator::Compl:
                                        result.first = builder.CreateNot(result.first);
                                        break;
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Float:
                                switch (op) {
                                    case Operator::Minus:
                                        result.first = builder.CreateFNeg(result.first);
                                        break;
                                    case Operator::Not:
                                        result.first = builder.CreateNot(toBoolean(result));
                                        result.second = AbaciValue::Boolean;
                                        break;
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                            case AbaciValue::Complex:
                                switch (op) {
                                    case Operator::Minus: {
                                        Function *complexMathFunc = module.getFunction("complexMath");
                                        auto cplx = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                                        builder.CreateCall(complexMathFunc,
                                            { cplx, ConstantInt::get(builder.getInt32Ty(), static_cast<int>(op)),
                                                result.first, ConstantPointerNull::get(PointerType::get(jit.getNamedType("struct.Complex"), 0)) });
                                        result.first = cplx;
                                        break;
                                    }
                                    default:
                                        UnexpectedError("Unknown operator.");
                                }
                                break;
                        default:
                            UnexpectedError("Not yet implemented");
                            break;
                        }
                    }
                    push(result);
                    break;
                }
```

Finally the `Boolean` operators (strictly speaking left associative, but always returning a Boolean result if the size of the `ExprList` is greater than 1) allows for (a limited number of) ternary expressions (`a = b = 1` or `0 <= b < 10`).

```cpp
                case ExprNode::Boolean: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.front());
                    auto result = pop();
                    if (expr.size() == 1) {
                        push(result);
                    }
                    else {
                        Value *bool_result = ConstantInt::get(builder.getInt1Ty(), true);
                        for (auto iter = ++expr.begin(); iter != expr.end();) {
                            auto op = std::get<Operator>(iter++->get());
                            (*this)(*iter++);
                            auto operand = pop();
                            auto type = (operand.second != result.second) ? promote(result, operand) : result.second;
                            switch (type) {
                                case AbaciValue::Boolean:
                                    switch (op) {
                                        case Operator::Equal:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpEQ(result.first, operand.first));
                                            break;
                                        case Operator::NotEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpNE(result.first, operand.first));
                                            break;
                                        case Operator::Less:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpULT(result.first, operand.first));
                                            break;
                                        case Operator::LessEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpULE(result.first, operand.first));
                                            break;
                                        case Operator::GreaterEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpUGE(result.first, operand.first));
                                            break;
                                        case Operator::Greater:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpUGT(result.first, operand.first));
                                            break;
                                        case Operator::And:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateAnd(result.first, operand.first));
                                            break;
                                        case Operator::Or:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateOr(result.first, operand.first));
                                            break;
                                        default:
                                            UnexpectedError("Unknown operator.");
                                    }
                                    break;
                                case AbaciValue::Integer:
                                    switch (op) {
                                        case Operator::Equal:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpEQ(result.first, operand.first));
                                            break;
                                        case Operator::NotEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpNE(result.first, operand.first));
                                            break;
                                        case Operator::Less:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpSLT(result.first, operand.first));
                                            break;
                                        case Operator::LessEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpSLE(result.first, operand.first));
                                            break;
                                        case Operator::GreaterEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpSGE(result.first, operand.first));
                                            break;
                                        case Operator::Greater:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateICmpSGT(result.first, operand.first));
                                            break;
                                        case Operator::And:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateAnd(toBoolean(result), toBoolean(operand)));
                                            break;
                                        case Operator::Or:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateOr(toBoolean(result), toBoolean(operand)));
                                            break;
                                        default:
                                            UnexpectedError("Unknown operator.");
                                    }
                                    break;
                                case AbaciValue::Float:
                                    switch (op) {
                                        case Operator::Equal:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateFCmpOEQ(result.first, operand.first));
                                            break;
                                        case Operator::NotEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateFCmpONE(result.first, operand.first));
                                            break;
                                        case Operator::Less:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateFCmpOLT(result.first, operand.first));
                                            break;
                                        case Operator::LessEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateFCmpOLE(result.first, operand.first));
                                            break;
                                        case Operator::GreaterEqual:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateFCmpOGE(result.first, operand.first));
                                            break;
                                        case Operator::Greater:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateFCmpOGT(result.first, operand.first));
                                            break;
                                        case Operator::And:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateAnd(toBoolean(result), toBoolean(operand)));
                                            break;
                                        case Operator::Or:
                                            bool_result = builder.CreateAnd(bool_result, builder.CreateOr(toBoolean(result), toBoolean(operand)));
                                            break;
                                        default:
                                            UnexpectedError("Unknown operator.");
                                    }
                                    break;
                                case AbaciValue::Complex: {
                                    Value *real_value1 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), result.first, 0));
                                    Value *imag_value1 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), result.first, 1));
                                    Value *real_value2 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), operand.first, 0));
                                    Value *imag_value2 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), operand.first, 1));
                                    switch (op) {
                                        case Operator::Equal:
                                            bool_result = builder.CreateAnd(bool_result,
                                                builder.CreateAnd(builder.CreateFCmpOEQ(real_value1, real_value2), builder.CreateFCmpOEQ(imag_value1, imag_value2)));
                                            break;
                                        case Operator::NotEqual:
                                            bool_result = builder.CreateAnd(bool_result,
                                                builder.CreateOr(builder.CreateFCmpONE(real_value1, real_value2), builder.CreateFCmpONE(imag_value1, imag_value2)));
                                            break;
                                        default:
                                            UnexpectedError("Unknown operator.");
                                    }
                                    break;
                                }
                                case AbaciValue::String: {
                                    Value *str1_ptr = builder.CreateLoad(builder.getInt8PtrTy(), builder.CreateStructGEP(jit.getNamedType("struct.String"), result.first, 0));
                                    Value *str2_ptr = builder.CreateLoad(builder.getInt8PtrTy(), builder.CreateStructGEP(jit.getNamedType("struct.String"), operand.first, 0));
                                    switch (op) {
                                        case Operator::Equal:
                                            bool_result = builder.CreateAnd(bool_result,
                                                builder.CreateICmpEQ(ConstantInt::get(builder.getInt32Ty(), 0),
                                                builder.CreateCall(module.getFunction("strcmp"), { str1_ptr, str2_ptr })));
                                            break;
                                        case Operator::NotEqual:
                                            bool_result = builder.CreateAnd(bool_result,
                                                builder.CreateICmpNE(ConstantInt::get(builder.getInt32Ty(), 0),
                                                builder.CreateCall(module.getFunction("strcmp"), { str1_ptr, str2_ptr })));
                                            break;
                                        default:
                                            UnexpectedError("Unknown operator.");
                                    }
                                    break;
                                }
                                default:
                                    UnexpectedError("Not yet implemented");
                                    break;
                            }
                            result.first = operand.first;
                            result.second = type;
                        }
                        result.first = bool_result;
                        result.second = AbaciValue::Boolean;
                        push(result);
                    }
                    break;
                }
                default:
                    UnexpectedError("Unknown node association type.");
            }
            break;
        }
        default:
            UnexpectedError("Bad node type.");
    }
}

```

Member function `promote()` takes two `StackType` reference parameters and modifies them in-place making sure they have the same type (currently always the maximum of the two: `Boolean` < `Integer` < `Floating` < `Complex`). The type returned is the new type of both. [Note: Some amount of code duplication is present.]

```cpp
AbaciValue::Type ExprCodeGen::promote(StackType& a, StackType& b) const {
    if (a.second == b.second) {
        return a.second;
    }
    auto type = std::max(a.second, b.second);
    switch (type) {
        case AbaciValue::Boolean:
        case AbaciValue::Integer:
            break;
        case AbaciValue::Float:
            switch (a.second) {
                case AbaciValue::Integer:
                    a.first = builder.CreateSIToFP(a.first, Type::getDoubleTy(module.getContext()));
                    break;
                default:
                    break;
            }
            switch (b.second) {
                case AbaciValue::Integer:
                    b.first = builder.CreateSIToFP(b.first, Type::getDoubleTy(module.getContext()));
                    break;
                default:
                    break;
            }
            break;
        case AbaciValue::Complex: {
            switch (a.second) {
                case AbaciValue::Integer: {
                    auto real = builder.CreateSIToFP(a.first, Type::getDoubleTy(module.getContext()));
                    auto imag = ConstantFP::get(builder.getDoubleTy(), 0.0);
                    a.first = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                    builder.CreateStore(real, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), a.first, 0));
                    builder.CreateStore(imag, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), a.first, 1));
                    break;
                }
                case AbaciValue::Float: {
                    auto real = a.first;
                    auto imag = ConstantFP::get(builder.getDoubleTy(), 0.0);
                    a.first = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                    builder.CreateStore(real, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), a.first, 0));
                    builder.CreateStore(imag, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), a.first, 1));
                    break;
                }
                default:
                    break;
            }
            switch (b.second) {
                case AbaciValue::Integer: {
                    auto real = builder.CreateSIToFP(b.first, Type::getDoubleTy(module.getContext()));
                    auto imag = ConstantFP::get(builder.getDoubleTy(), 0.0);
                    b.first = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                    builder.CreateStore(real, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), b.first, 0));
                    builder.CreateStore(imag, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), b.first, 1));
                    break;
                }
                case AbaciValue::Float: {
                    auto real = b.first;
                    auto imag = ConstantFP::get(builder.getDoubleTy(), 0.0);
                    b.first = builder.CreateAlloca(jit.getNamedType("struct.Complex"));
                    builder.CreateStore(real, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), b.first, 0));
                    builder.CreateStore(imag, builder.CreateStructGEP(jit.getNamedType("struct.Complex"), b.first, 1));
                    break;
                }
                default:
                    break;
            }
            break;
        }
        default:
            UnexpectedError("Not yet implemented.");
    }
    return a.second = b.second = type;
}

```

Member function `toBoolean()` returns a `Value*` which is always a Boolean based on the type and value of the single reference parameter (`0`, `0.0` and `""` produce `false`, everythin else is `true`).

```cpp
Value *ExprCodeGen::toBoolean(StackType& v) const {
    Value *boolean;
    switch (v.second) {
        case AbaciValue::Boolean:
            boolean = v.first;
            break;
        case AbaciValue::Integer:
            boolean = builder.CreateICmpNE(v.first, ConstantInt::get(builder.getInt64Ty(), 0));
            break;
        case AbaciValue::Float:
            boolean = builder.CreateFCmpONE(v.first, ConstantFP::get(builder.getDoubleTy(), 0.0));
            break;
        case AbaciValue::String: {
            Value *length = builder.CreateLoad(builder.getInt64Ty(), builder.CreateStructGEP(jit.getNamedType("struct.String"), v.first, 1));
            boolean = builder.CreateICmpNE(length, ConstantInt::get(builder.getInt64Ty(), 0));
            break;
        }
        default:
            UnexpectedError("Not yet implemented.");
    }
    return boolean;
}

}
```

### `StmtCodeGen.cpp`

The whole of the `llvm` namespace is again made available, plus most of the `abaci::ast` namespace. The `operator()` for `StmtList` creates a new `DefineScope` and a new `Scope` before iterating over the statements in the block.

```cpp
#include "CodeGen.hpp"
#include "utility/Utility.hpp"
#include <llvm/IR/Constants.h>

using namespace llvm;

namespace abaci::codegen {

using abaci::ast::ValueCall;
using abaci::ast::ExprNode;
using abaci::ast::ExprList;
using abaci::ast::PrintList;
using abaci::ast::StmtNode;
using abaci::ast::StmtList;
using abaci::ast::CommentStmt;
using abaci::ast::PrintStmt;
using abaci::ast::PrintList;
using abaci::ast::InitStmt;
using abaci::ast::AssignStmt;
using abaci::ast::IfStmt;
using abaci::ast::WhileStmt;
using abaci::ast::RepeatStmt;
using abaci::ast::CaseStmt;
using abaci::ast::WhenStmt;
using abaci::ast::Function;
using abaci::ast::FunctionCall;
using abaci::ast::ReturnStmt;
using abaci::ast::ExprFunction;
using abaci::ast::Class;
using abaci::utility::Operator;

void StmtCodeGen::operator()(const StmtList& stmts) const {
    if (!stmts.empty()) {
        Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
        Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
        environment->beginDefineScope();
        builder.CreateCall(module.getFunction("beginScope"), { typed_environment_ptr });
        for (const auto& stmt : stmts) {
            (*this)(stmt);
        }
        builder.CreateCall(module.getFunction("endScope"), { typed_environment_ptr });
        environment->endDefineScope();
    }
}

```

The specialization of `codeGen()` for `PrintStmt` is more general than it needs to be, and evaluates and outputs all `ExprNode`s within the print expression separated by commas. If the `PrintList` does not end with a semi-colon, a newline is output from library calls.

```cpp
template<>
void StmtCodeGen::codeGen([[maybe_unused]] const CommentStmt& comment) const {
}

template<>
void StmtCodeGen::codeGen(const PrintStmt& print) const {
    PrintList print_data{ print.expression };
    print_data.insert(print_data.end(), print.format.begin(), print.format.end());
    for (auto field : print_data) {
        switch (field.index()) {
            case 0: {
                ExprCodeGen expr(jit);
                expr(std::get<ExprNode>(field));
                auto result = expr.get();
                auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
                builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
                builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), result.second), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
                builder.CreateCall(module.getFunction("printValue"), { abaci_value });
                break;
            }
            case 1: {
                auto oper = std::get<Operator>(field);
                switch (oper) {
                    case Operator::Comma:
                        builder.CreateCall(module.getFunction("printComma"));
                        break;
                    case Operator::SemiColon:
                        break;
                    default:
                        UnexpectedError("Bad print operator.");
                }
                break;
            }
            default:
                UnexpectedError("Bad field.");
                break;
        }
    }
    if (!print_data.empty() && print_data.back().index() == 1) {
        if (std::get<Operator>(print_data.back()) != Operator::SemiColon && std::get<Operator>(print_data.back()) != Operator::Comma) {
            builder.CreateCall(module.getFunction("printLn"));
        }
    }
    else {
        builder.CreateCall(module.getFunction("printLn"));
    }
}

```

The specialization for `InitStmt` evaluates the right-hand side and then stores the value (masked with `AbaciValue::Constant` for `let =`) in the current scope. In the case of global scope, the type will have already been set by `TypeCodeGen`.

```cpp
template<>
void StmtCodeGen::codeGen(const InitStmt& define) const {
    Constant *name = ConstantDataArray::getString(module.getContext(), define.name.get().c_str());
    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
    builder.CreateStore(name, str);
    ExprCodeGen expr(jit);
    expr(define.value);
    auto result = expr.get();
    auto type = static_cast<AbaciValue::Type>(result.second | ((define.assign == Operator::Equal) ? AbaciValue::Constant : 0));
    if (environment->getCurrentDefineScope()->getEnclosing()) {
        environment->getCurrentDefineScope()->setType(define.name.get(), type);
    }
    auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
    builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), type), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
    builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), abaci_value, ConstantInt::get(builder.getInt1Ty(), true) });
}

```

Use of `AssignStmt` implies the variable already exists, and checks are made for this. The rest of the code is very similar to that for `InitStmt`.

```cpp
template<>
void StmtCodeGen::codeGen(const AssignStmt& assign) const {
    if (!environment->getCurrentDefineScope()->isDefined(assign.name.get())) {
        LogicError("No such variable.");
    }
    else if (environment->getCurrentDefineScope()->getType(assign.name.get()) & AbaciValue::Constant) {
        LogicError("Cannot assign to constant.");
    }
    Constant *name = ConstantDataArray::getString(module.getContext(), assign.name.get().c_str());
    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
    builder.CreateStore(name, str);
    ExprCodeGen expr(jit);
    expr(assign.value);
    auto result = expr.get();
    if (environment->getCurrentDefineScope()->getType(assign.name.get()) != result.second) {
        LogicError("Cannot assign to variable with different type.");
    }
    auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
    builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), result.second), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
    builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), abaci_value, ConstantInt::get(builder.getInt1Ty(), false) });
}

```

An `IfStmt` requires evaluation of the condition, and conversion of this to a Boolean value if not already. Three "Basic Blocks" are needed to fulfil the logic of `if`/`else`/`endif`, and LLVM blocks are not allowed to "fall through" necessitating the use of a conditional branch and two unconditional branches.

```cpp
template<>
void StmtCodeGen::codeGen(const IfStmt& if_stmt) const {
    ExprCodeGen expr(jit);
    expr(if_stmt.condition);
    auto result = expr.get();
    Value *condition;
    if (result.second == AbaciValue::Boolean) {
        condition = result.first;
    }
    else {
        condition = expr.toBoolean(result);
    }
    BasicBlock *true_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    BasicBlock *false_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    BasicBlock *merge_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    builder.CreateCondBr(condition, true_block, false_block);
    builder.SetInsertPoint(true_block);
    (*this)(if_stmt.true_test);
    builder.CreateBr(merge_block);
    builder.SetInsertPoint(false_block);
    (*this)(if_stmt.false_test);
    builder.CreateBr(merge_block);
    builder.SetInsertPoint(merge_block);
}

```

The simplest looping construct is `WhileStmt`, again requiring three Basic Blocks and evaluating a condition (inside the `pre_block`), converting this to Boolean as necessary. A sub-scope is created for the whole of the `WhileStmt` meaning that there is none required for the body itself, so the statements are iterated over instead of calling `operator()` for `StmtList`.

```cpp
template<>
void StmtCodeGen::codeGen(const WhileStmt& while_stmt) const {
    BasicBlock *pre_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    BasicBlock *loop_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    BasicBlock *post_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
    environment->beginDefineScope();
    builder.CreateCall(module.getFunction("beginScope"), { typed_environment_ptr });
    builder.CreateBr(pre_block);
    builder.SetInsertPoint(pre_block);
    ExprCodeGen expr(jit);
    expr(while_stmt.condition);
    auto result = expr.get();
    Value *condition;
    if (result.second == AbaciValue::Boolean) {
        condition = result.first;
    }
    else {
        condition = expr.toBoolean(result);
    }
    builder.CreateCondBr(condition, loop_block, post_block);
    builder.SetInsertPoint(loop_block);
    for (const auto& stmt : while_stmt.loop_block) {
        (*this)(stmt);
    }
    builder.CreateBr(pre_block);
    builder.SetInsertPoint(post_block);
    builder.CreateCall(module.getFunction("endScope"), { typed_environment_ptr });
    environment->endDefineScope();
}

```

Support for `repeat`/`until` is provided by a specialization for `RepeatStmt`, being a "back-to-front" `while` loop with the condition logic inverted (the conditional branch jumps to `post_block` for `true`). Only two Basic Blocks are required and as for `WhileStmt` a scope is created for the whole statement meaning none is necessary for the loop body.

```cpp
template<>
void StmtCodeGen::codeGen(const RepeatStmt& repeat_stmt) const {
    BasicBlock *loop_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    BasicBlock *post_block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
    environment->beginDefineScope();
    builder.CreateCall(module.getFunction("beginScope"), { typed_environment_ptr });
    builder.CreateBr(loop_block);
    builder.SetInsertPoint(loop_block);
    for (const auto& stmt : repeat_stmt.loop_block) {
        (*this)(stmt);
    }
    ExprCodeGen expr(jit);
    expr(repeat_stmt.condition);
    auto result = expr.get();
    Value *condition;
    if (result.second == AbaciValue::Boolean) {
        condition = result.first;
    }
    else {
        condition = expr.toBoolean(result);
    }
    builder.CreateCondBr(condition, post_block, loop_block);
    builder.SetInsertPoint(post_block);
    builder.CreateCall(module.getFunction("endScope"), { typed_environment_ptr });
    environment->endDefineScope();
}

```

The specialization for `CaseStmt` is the most involved, creating a `std::vector<BasicBlock*>` and referring to them by index (`when` clauses times 2 plus `else` clause if present plus 1). The `when` clauses are evaluated in the same order written in the Abaci0 program, a match causes a jump to the last Basic Block.

```cpp
template<>
void StmtCodeGen::codeGen(const CaseStmt& case_stmt) const {
    std::vector<BasicBlock*> case_blocks(case_stmt.matches.size() * 2 + 1 + !case_stmt.unmatched.empty());
    for (auto& block : case_blocks) {
        block = BasicBlock::Create(jit.getContext(), "", jit.getFunction());
    }
    ExprCodeGen expr(jit);
    expr(case_stmt.case_value);
    auto result = expr.get();
    builder.CreateBr(case_blocks.front());
    for (int block_number = 0; const auto& when : case_stmt.matches) {
        builder.SetInsertPoint(case_blocks.at(block_number * 2));
        auto match_result = result;
        ExprCodeGen expr(jit);
        expr(when.expression);
        auto when_result = expr.get();
        Value *is_match;
        switch (expr.promote(when_result, match_result)) {
            case AbaciValue::Boolean:
            case AbaciValue::Integer:
                is_match = builder.CreateICmpEQ(when_result.first, match_result.first);
                break;
            case AbaciValue::Float:
                is_match = builder.CreateFCmpOEQ(when_result.first, match_result.first);
                break;
            case AbaciValue::Complex: {
                Value *real_value1 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), when_result.first, 0));
                Value *imag_value1 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), when_result.first, 1));
                Value *real_value2 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), match_result.first, 0));
                Value *imag_value2 = builder.CreateLoad(builder.getDoubleTy(), builder.CreateStructGEP(jit.getNamedType("struct.Complex"), match_result.first, 1));
                is_match = builder.CreateAnd(builder.CreateFCmpOEQ(real_value1, real_value2), builder.CreateFCmpOEQ(imag_value1, imag_value2));
                break;
            }
            case AbaciValue::String: {
                Value *str1_ptr = builder.CreateLoad(builder.getInt8PtrTy(), builder.CreateStructGEP(jit.getNamedType("struct.String"), when_result.first, 0));
                Value *str2_ptr = builder.CreateLoad(builder.getInt8PtrTy(), builder.CreateStructGEP(jit.getNamedType("struct.String"), match_result.first, 0));
                is_match = builder.CreateICmpEQ(ConstantInt::get(builder.getInt32Ty(), 0),
                    builder.CreateCall(module.getFunction("strcmp"), { str1_ptr, str2_ptr }));
                break;
            }
            default:
                UnexpectedError("Not yet implemented.");
        }
        builder.CreateCondBr(is_match, case_blocks.at(block_number * 2 + 1), case_blocks.at(block_number * 2 + 2));
        builder.SetInsertPoint(case_blocks.at(block_number * 2 + 1));
        (*this)(when.block);
        builder.CreateBr(case_blocks.back());
        ++block_number;
    }
    if (!case_stmt.unmatched.empty()) {
        builder.SetInsertPoint(case_blocks.at(case_blocks.size() - 2));
        (*this)(case_stmt.unmatched);
        builder.CreateBr(case_blocks.back());
    }
    builder.SetInsertPoint(case_blocks.back());
}

```

There is no code for `Function` as this is handled by class `TypeCodeGen`.

```cpp
template<>
void StmtCodeGen::codeGen([[maybe_unused]] const Function& function) const {
}

```

The specialization for `FunctionCall` matches that for `ValueCall` in class `ExprCodeGen`, including support for `_return` to prevent errors if calling value-returning functions directly.

```cpp
template<>
void StmtCodeGen::codeGen(const FunctionCall& function_call) const {
    const auto& cache_function = jit.getCache()->getFunction(function_call.name);
    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
    std::vector<StackType> arguments;
    std::vector<AbaciValue::Type> types;
    for (const auto& arg : function_call.args) {
        ExprCodeGen expr(jit);
        expr(arg);
        arguments.push_back(expr.get());
        types.push_back(arguments.back().second);
    }
    environment->beginDefineScope();
    builder.CreateCall(module.getFunction("beginScope"), { typed_environment_ptr });
    for (auto arg = arguments.begin(); const auto& parameter : cache_function.parameters) {
        Constant *name = ConstantDataArray::getString(module.getContext(), parameter.get().c_str());
        AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
        builder.CreateStore(name, str);
        auto result = *arg++;
        auto type = static_cast<AbaciValue::Type>(result.second | AbaciValue::Constant);
        environment->getCurrentDefineScope()->setType(parameter.get(), type);
        auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
        builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
        builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), type), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
        Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
        Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
        builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), abaci_value, ConstantInt::get(builder.getInt1Ty(), true) });
    }
    auto type = jit.getCache()->getFunctionInstantiationType(function_call.name, types);
    environment->getCurrentDefineScope()->setType("_return", type);
    Constant *name = ConstantDataArray::getString(module.getContext(), "_return");
    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
    builder.CreateStore(name, str);
    auto return_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
    builder.CreateStore(ConstantInt::get(builder.getInt64Ty(), 0), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), return_value, 0));
    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), type), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), return_value, 1));
    builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), return_value, ConstantInt::get(builder.getInt1Ty(), true) });
    std::string function_name{ mangled(function_call.name, types) };
    builder.CreateCall(module.getFunction(function_name), {});
    builder.CreateCall(module.getFunction("endScope"), { typed_environment_ptr });
    environment->endDefineScope();
}

```

The code for `ReturnStmt` evaluates the expression and assigns this to `_return`. A check for the nesting depth (set by class `TypeCodeGen`) allows for the correct number of calls to library function `endScope()`, meaning that `return` statements within lexical sub-scopes are permitted.

```cpp
template<>
void StmtCodeGen::codeGen(const ReturnStmt& return_stmt) const {
    ExprCodeGen expr(jit);
    expr(return_stmt.expression);
    auto result = expr.get();
    Constant *name = ConstantDataArray::getString(module.getContext(), "_return");
    AllocaInst *str = builder.CreateAlloca(name->getType(), nullptr);
    builder.CreateStore(name, str);
    auto abaci_value = builder.CreateAlloca(jit.getNamedType("struct.AbaciValue"));
    builder.CreateStore(result.first, builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 0));
    builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), result.second & AbaciValue::TypeMask), builder.CreateStructGEP(jit.getNamedType("struct.AbaciValue"), abaci_value, 1));
    Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
    Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(jit.getNamedType("struct.Environment"), 0));
    builder.CreateCall(module.getFunction("setVariable"), { typed_environment_ptr, builder.CreateBitCast(str, builder.getInt8PtrTy()), abaci_value, ConstantInt::get(builder.getInt1Ty(), false) });
    for (int i = depth; i < (return_stmt.depth - 1); ++i) {
        builder.CreateCall(module.getFunction("endScope"), { typed_environment_ptr });
    }
    builder.CreateBr(exitBlock);
}

```

For the same reason as for `Function`, there is no code for `ExprFunction` or `Class`.

```cpp
template<>
void StmtCodeGen::codeGen([[maybe_unused]] const ExprFunction& expression_function) const {
}

template<>
void StmtCodeGen::codeGen([[maybe_unused]] const Class& class_template) const {
}

```

Finally, `operator()` for `StmtNode` performs a number of `dynamic_cast`s to determine which specialization to call, this function is by necessity defined after all of the specializations.

```cpp
void StmtCodeGen::operator()(const StmtNode& stmt) const {
    const auto *stmt_data = stmt.get();
    if (dynamic_cast<const CommentStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const CommentStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const PrintStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const PrintStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const InitStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const InitStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const AssignStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const AssignStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const IfStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const IfStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const WhileStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const WhileStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const RepeatStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const RepeatStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const CaseStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const CaseStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const Function*>(stmt_data)) {
        codeGen(dynamic_cast<const Function&>(*stmt_data));
    }
    else if (dynamic_cast<const FunctionCall*>(stmt_data)) {
        codeGen(dynamic_cast<const FunctionCall&>(*stmt_data));
    }
    else if (dynamic_cast<const ReturnStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const ReturnStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const ExprFunction*>(stmt_data)) {
        codeGen(dynamic_cast<const ExprFunction&>(*stmt_data));
    }
    else if (dynamic_cast<const Class*>(stmt_data)) {
        codeGen(dynamic_cast<const Class&>(*stmt_data));
    }
    else {
        UnexpectedError("Bad StmtNode type.");
    }
}

}
```

### `TypeCodeGen.cpp`

The code for these functions is best examined with reference to the other two implementation files in this directory. It is mostly a "bare-bones" implementation using the minimal amount of code to ensure that function return-type information can be gleaned from the AST.

The layout of `TypeEvalGen::operator()` exactly matches that for class `ExprCodeGen`.

```cpp
#include "CodeGen.hpp"
#include "utility/Utility.hpp"
#include <llvm/IR/Constants.h>

using namespace llvm;

namespace abaci::codegen {

using abaci::ast::ValueCall;
using abaci::ast::ExprNode;
using abaci::ast::ExprList;
using abaci::ast::PrintList;
using abaci::ast::StmtNode;
using abaci::ast::StmtList;
using abaci::ast::CommentStmt;
using abaci::ast::PrintStmt;
using abaci::ast::PrintList;
using abaci::ast::InitStmt;
using abaci::ast::AssignStmt;
using abaci::ast::IfStmt;
using abaci::ast::WhileStmt;
using abaci::ast::RepeatStmt;
using abaci::ast::CaseStmt;
using abaci::ast::WhenStmt;
using abaci::ast::Function;
using abaci::ast::FunctionCall;
using abaci::ast::ReturnStmt;
using abaci::ast::ExprFunction;
using abaci::ast::Class;
using abaci::utility::Operator;

void TypeEvalGen::operator()(const abaci::ast::ExprNode& node) const {
    switch (node.get().index()) {
        case ExprNode::ValueNode:
            push(std::get<AbaciValue>(node.get()).type);
            break;
        case ExprNode::VariableNode:
            push(static_cast<AbaciValue::Type>(environment->getCurrentDefineScope()->getType(std::get<Variable>(node.get()).get()) & AbaciValue::TypeMask));
            break;
```

A `CallNode` causes a scope switch to implement an instantiation using `addFunctionInstantiation()`, which may itself call `TypeEvalGen::operator()`. The head recursion thus implied means that the leaf functions are listed first ready for definition in correct order by `JIT::initialize()`.

```cpp
        case ExprNode::CallNode: {
            const auto& call = std::get<ValueCall>(node.get());
            switch(cache->getCacheType(call.name)) {
                case Cache::CacheFunction: {
                    const auto& cache_function = cache->getFunction(call.name);
                    std::vector<AbaciValue::Type> types;
                    for (const auto& arg : call.args) {
                        TypeEvalGen expr(environment, cache);
                        expr(arg);
                        types.push_back(expr.get());
                    }
                    auto current_scope = environment->getCurrentDefineScope();
                    environment->beginDefineScope(environment->getGlobalDefineScope());
                    for (auto arg_type = types.begin(); const auto& parameter : cache_function.parameters) {
                        auto type = static_cast<AbaciValue::Type>(*arg_type++ | AbaciValue::Constant);
                        environment->getCurrentDefineScope()->setType(parameter.get(), type);
                    }
                    cache->addFunctionInstantiation(call.name, types, environment);
                    auto return_type = cache->getFunctionInstantiationType(call.name, types);
                    environment->getCurrentDefineScope()->setType("_return", return_type);
                    environment->setCurrentDefineScope(current_scope);
                    push(return_type);
                    break;
                }
                case Cache::CacheClass: {
                    UnexpectedError("Instantiation of classes not yet implemented.");
                    break;
                }
                default:
                    UnexpectedError("Not a function or class.");
            }
            break;
        }
```

Implicit promotions based upon `Operator` types are handled for `ListNode`.

```cpp
        case ExprNode::ListNode: {
            switch (node.getAssociation()) {
                case ExprNode::Left: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.front());
                    auto type = pop();
                    for (auto iter = ++expr.begin(); iter != expr.end();) {
                        auto op = std::get<Operator>(iter++->get());
                        (*this)(*iter++);
                        type = promote(type, pop());
                        if (type == AbaciValue::Integer && op == Operator::Divide) {
                            type = AbaciValue::Float;
                        }
                    }
                    push(type);
                    break;
                }
                case ExprNode::Right: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.back());
                    auto type = pop();
                    for (auto iter = ++expr.rbegin(); iter != expr.rend();) {
                        ++iter;
                        (*this)(*iter++);
                        type = promote(type, AbaciValue::Float);
                        type = promote(type, pop());
                    }
                    push(type);
                    break;
                }
                case ExprNode::Unary: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.back());
                    auto type = pop();
                    for (auto iter = ++expr.rbegin(); iter != expr.rend();) {
                        auto op = std::get<Operator>(iter++->get());
                        if (op == Operator::Not) {
                            type = AbaciValue::Boolean;
                        }
                    }
                    push(type);
                    break;
                }
                case ExprNode::Boolean: {
                    const auto& expr = std::get<ExprList>(node.get());
                    (*this)(expr.front());
                    auto type = pop();
                    if (expr.size() > 1) {
                        type = AbaciValue::Boolean;
                    }
                    push(type);
                    break;
                }
                default:
                    UnexpectedError("Unknown node type.");
            }
            break;
        }
        default:
            UnexpectedError("Bad node type.");
    }
}

```

A simplified `promote()` produces the same results as for `ExprCodeGen::promote()`.

```cpp
AbaciValue::Type TypeEvalGen::promote(AbaciValue::Type a, AbaciValue::Type b) const {
    if (a == b) {
        return a;
    }
    else if (a < AbaciValue::String && b < AbaciValue::String) {
        return std::max(a, b);
    }
    else {
        LogicError("Bad types.");
    }
}

```

The second part of this implementation file is for class `TypeCodeGen`, starting with `operator()` for `StmtList` which creates a new `DefineScope`.

```cpp
void TypeCodeGen::operator()(const abaci::ast::StmtList& stmts) const {
    if (!stmts.empty()) {
        environment->beginDefineScope();
        for (const auto& stmt : stmts) {
            (*this)(stmt);
        }
        environment->endDefineScope();
    }
}

```

The specialization for `PrintStmt` evaluates the expressions, and needs to because it may be a function call. Similarly for the other specializations, all expressions are evaluated by a `TypeEvalGen` instance.

```cpp
template<>
void TypeCodeGen::codeGen([[maybe_unused]] const CommentStmt& comment) const {
}

template<>
void TypeCodeGen::codeGen(const PrintStmt& print) const {
    PrintList print_data{ print.expression };
    print_data.insert(print_data.end(), print.format.begin(), print.format.end());
    for (auto field : print_data) {
        switch (field.index()) {
            case 0: {
                TypeEvalGen expr(environment, cache);
                expr(std::get<ExprNode>(field));
                break;
            }
            case 1:
                break;
            default:
                UnexpectedError("Bad field.");
        }
    }
}

template<>
void TypeCodeGen::codeGen(const InitStmt& define) const {
    TypeEvalGen expr(environment, cache);
    expr(define.value);
    auto result = expr.get();
    auto type = static_cast<AbaciValue::Type>(result | ((define.assign == Operator::Equal) ? AbaciValue::Constant : 0));
    environment->getCurrentDefineScope()->setType(define.name.get(), type);
}

template<>
void TypeCodeGen::codeGen(const AssignStmt& assign) const {
    if (!environment->getCurrentDefineScope()->isDefined(assign.name.get())) {
        UnexpectedError("No such variable.");
    }
    else if (environment->getCurrentDefineScope()->getType(assign.name.get()) & AbaciValue::Constant) {
        UnexpectedError("Cannot assign to constant.");
    }
    TypeEvalGen expr(environment, cache);
    expr(assign.value);
    auto result = expr.get();
    if (environment->getCurrentDefineScope()->getType(assign.name.get()) != result) {
        UnexpectedError("Cannot assign to variable with different type.");
    }
}

template<>
void TypeCodeGen::codeGen(const IfStmt& if_stmt) const {
    TypeEvalGen expr(environment, cache);
    expr(if_stmt.condition);
    (*this)(if_stmt.true_test);
    (*this)(if_stmt.false_test);
}

template<>
void TypeCodeGen::codeGen(const WhileStmt& while_stmt) const {
    environment->beginDefineScope();
    TypeEvalGen expr(environment, cache);
    expr(while_stmt.condition);
    for (const auto& stmt : while_stmt.loop_block) {
        (*this)(stmt);
    }
    environment->endDefineScope();
}

template<>
void TypeCodeGen::codeGen(const RepeatStmt& repeat_stmt) const {
    environment->beginDefineScope();
    for (const auto& stmt : repeat_stmt.loop_block) {
        (*this)(stmt);
    }
    TypeEvalGen expr(environment, cache);
    expr(repeat_stmt.condition);
    environment->endDefineScope();
}

template<>
void TypeCodeGen::codeGen(const CaseStmt& case_stmt) const {
    TypeEvalGen expr(environment, cache);
    expr(case_stmt.case_value);
    for (const auto& when : case_stmt.matches) {
        TypeEvalGen expr(environment, cache);
        expr(when.expression);
        (*this)(when.block);
    }
    if (!case_stmt.unmatched.empty()) {
        (*this)(case_stmt.unmatched);
    }
}

```

The specialization for `Function` performs the simple but important step of adding the function name, parameter names and function body to the cache.

```cpp
template<>
void TypeCodeGen::codeGen(const Function& function) const {
    cache->addFunctionTemplate(function.name, function.parameters, function.function_body);
}

```

The code for `FunctionCall` is similar to that for handling `ValueCall` in class `TypeEvalGen`.

```cpp
template<>
void TypeCodeGen::codeGen(const FunctionCall& function_call) const {
    const auto& cache_function = cache->getFunction(function_call.name);
    std::vector<AbaciValue::Type> types;
    for (const auto& arg : function_call.args) {
        TypeEvalGen expr(environment, cache);
        expr(arg);
        types.push_back(expr.get());
    }
    auto current_scope = environment->getCurrentDefineScope();
    environment->beginDefineScope(environment->getGlobalDefineScope());
    for (auto arg_type = types.begin(); const auto& parameter : cache_function.parameters) {
        auto type = static_cast<AbaciValue::Type>(*arg_type++ | AbaciValue::Constant);
        environment->getCurrentDefineScope()->setType(parameter.get(), type);
    }
    cache->addFunctionInstantiation(function_call.name, types, environment);
    auto return_type = cache->getFunctionInstantiationType(function_call.name, types);
    environment->getCurrentDefineScope()->setType("_return", return_type);
    environment->setCurrentDefineScope(current_scope);
}

```

The specialization for `ReturnStmt`causes the return expression to be evaluated for its type, setting the member `return_type` and `type_is_set`. The `depth` member in the AST is also set here (different instantiations of the same function would always have the same depth due to the scoping rules for functions).

```cpp
template<>
void TypeCodeGen::codeGen(const ReturnStmt& return_stmt) const {
    TypeEvalGen expr(environment, cache);
    expr(return_stmt.expression);
    auto result = expr.get();
    if (type_is_set && return_type != result) {
        UnexpectedError("Function return type already set to different type.");
    }
    return_type = result;
    type_is_set = true;
    return_stmt.depth = environment->getCurrentDefineScope()->getDepth();
}

```

It's a fact that Abaci0's `ExprFunction`s (`let f(n) -> n + 1`) are just regular functions, requiring unpacking of the expression and putting it into a `return` statement inside a function body. This is added to the cache as for `Function`.

```cpp
template<>
void TypeCodeGen::codeGen(const ExprFunction& expression_function) const {
    auto *return_stmt = new ReturnStmt();
    return_stmt->expression = expression_function.expression;
    return_stmt->depth = 1;
    StmtList function_body{ StmtNode(return_stmt) };
    cache->addFunctionTemplate(expression_function.name, expression_function.parameters, function_body);
}

```

The code for `Class` pushes the methods (qualified with the class name and a dot) as well as the class template itself into the cache. [Note: Class instantiations and method access are not currently implemented.]

```cpp
template<>
void TypeCodeGen::codeGen(const Class& class_template) const {
    std::vector<std::string> method_names;
    for (const auto& method : class_template.methods) {
        method_names.push_back(method.name);
        cache->addFunctionTemplate(class_template.name + '.' + method.name, method.parameters, method.function_body);
    }
    cache->addClassTemplate(class_template.name, class_template.variables, method_names);
}

```

The `operator()` for `StmtNode` is identical to that for class `StmtCodeGen`.

```cpp
void TypeCodeGen::operator()(const StmtNode& stmt) const {
    const auto *stmt_data = stmt.get();
    if (dynamic_cast<const CommentStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const CommentStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const PrintStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const PrintStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const InitStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const InitStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const AssignStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const AssignStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const IfStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const IfStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const WhileStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const WhileStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const RepeatStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const RepeatStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const CaseStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const CaseStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const Function*>(stmt_data)) {
        codeGen(dynamic_cast<const Function&>(*stmt_data));
    }
    else if (dynamic_cast<const FunctionCall*>(stmt_data)) {
        codeGen(dynamic_cast<const FunctionCall&>(*stmt_data));
    }
    else if (dynamic_cast<const ReturnStmt*>(stmt_data)) {
        codeGen(dynamic_cast<const ReturnStmt&>(*stmt_data));
    }
    else if (dynamic_cast<const ExprFunction*>(stmt_data)) {
        codeGen(dynamic_cast<const ExprFunction&>(*stmt_data));
    }
    else if (dynamic_cast<const Class*>(stmt_data)) {
        codeGen(dynamic_cast<const Class&>(*stmt_data));
    }
    else {
        UnexpectedError("Bad StmtNode type.");
    }
}

}
```

## Directory `engine`

The code in this directory is concerned with interfacing the LLVM API-generated code with the running executable. The code in `Cache.hpp` and `Cache.cpp` store parts of the AST which are necessary to complete a program or statement "module", being all of the functions which they may call. Function return-type and `DefineScope` information is also kept here.

The most important parts of the interface are located in `JIT.hpp` and `JIT.cpp`. An instance of class `JIT` is created in the driver and passed by reference to class `StmtCodeGen` (and indirectly to class `ExprCodeGen`) in order for LLVM instructions to be inserted at the correct place. This instance returns a function which can be called to execute all of the compiled Abaci0 code.

### `Cache.hpp`

Three nested classes are defined: `Class`, `Function` and `Instantiation`. The member variables of the (usually single) `Cache` instance in the client code are containers for any number of instances of these three types. Member functions to add to or access these containers are provided.

```cpp
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
        std::vector<AbaciValue::Type> parameter_types;
        AbaciValue::Type return_type;
        std::shared_ptr<Environment::DefineScope> scope;
    };
    enum Type{ CacheClass, CacheFunction, CacheNone };
    Cache() = default;
    void addClassTemplate(const std::string& name, const std::vector<Variable>& variables, const std::vector<std::string>& methods);
    void addFunctionTemplate(const std::string& name, const std::vector<Variable>& parameters, const StmtList& body);
    void addFunctionInstantiation(const std::string& name, const std::vector<AbaciValue::Type>& types, Environment *environment);
    AbaciValue::Type getFunctionInstantiationType(const std::string& name, const std::vector<AbaciValue::Type>& types) const;
    std::shared_ptr<Environment::DefineScope> getFunctionInstantiationScope(const std::string& name, const std::vector<AbaciValue::Type>& types) const;
    Type getCacheType(const std::string& name) const;
    const Function& getFunction(const std::string& name) const;
    const Class& getClass(const std::string& name) const;
    const auto& getInstantiations() const { return instantiations; }
    void clearInstantiations() { instantiations.clear(); }
private:
    std::unordered_map<std::string,Class> classes;
    std::unordered_map<std::string,Function> functions;
    std::vector<Instantiation> instantiations;
};

}

#endif
```

### `Cache.cpp`

Member functions `addClassTemplate()` and `addFunctionTemplate()` allow adding from the AST to the cache.

```cpp
#include "Cache.hpp"
#include "codegen/CodeGen.hpp"
#include "utility/Report.hpp"

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

```

Function `addFunctionInstantiation()` is called with a function name, actual parameter types and and `Environment*`, creating a new instantiation record if the mangled name does not match any already present. This record obtains a return type value by calling `TypeCodeGen::operator()`.

```cpp
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
                TypeCodeGen gen_return_type(environment, this);
                for (const auto& function_stmt : iter->second.body) {
                    gen_return_type(function_stmt);
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

```

The "getters" for this class allow reading from the cache containers.

```cpp
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

}
```

### `JIT.hpp`

This class contains the basis for activating an `llvm::orc::LLJIT`, with all of the LLVM types needed to create a callable JIT-compiled function dynamically. It is created with a module name, function name, `Environment` pointer and `Cache` pointer, initializing an `LLVMContext` and `Module`. All of these data members are accessible via "getters", along with `getNamedType()` for Abaci0 types `struct.Complex`, `struct.String` etc. used by classes `ExprCodeGen` and `StmtCodeGen`.

```cpp
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
```

The constructor calls `JIT::initialize()` which performs the task of initializing the module with types and callable functions, as well as compiling all of the Abaci0 functions instantiated for the program or statement. When this function returns, code is inserted in the named function (function calls are allowed but new definitions are not, by this point).

```cpp
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
```

The call to `JIT::getExecFunction` calls finalization of the named function, as well as creation of a JIT module and dynamic library catering for any library calls the compiled code may make. It then returns a typed pointer for calling by the client code.

```cpp
    StmtFunctionType getExecFunction();
};

}

#endif
```

### `JIT.cpp`

Function `initialize()` contains boilerplate code related to setting values for library function types and linkage as well as the types used to allocate `AbaciValue` instances on the LLVM side. A loop over all the function instantiations stored in the cache involves calls to class `StmtCodeGen` (with `exit_block` and `DefineScope` depth). If the function does not end with `return` then a branch into `exit_block` is needed. The function ends with starting the statement (REPL) or block (file) execution function, setting an insert point for further code generated.

```cpp
#include "JIT.hpp"
#include "codegen/CodeGen.hpp"
#include "lib/Abaci.hpp"
#include "utility/Report.hpp"
#include <llvm/IR/Verifier.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/IR/Type.h>
#include <stdexcept>
#include <memory>
#include <cmath>
#include <cstring>

using namespace llvm;
using namespace llvm::orc;

namespace abaci::engine {

using abaci::codegen::StmtCodeGen;
using abaci::ast::ReturnStmt; 

void JIT::initialize() {
    FunctionType *pow_func_type = FunctionType::get(builder.getDoubleTy(), { builder.getDoubleTy(), builder.getDoubleTy() }, false);
    Function::Create(pow_func_type, Function::ExternalLinkage, "pow", module.get());
    FunctionType *strcmp_func_type = FunctionType::get(builder.getInt32Ty(), { builder.getInt8PtrTy(), builder.getInt8PtrTy() }, false);
    Function::Create(strcmp_func_type, Function::ExternalLinkage, "strcmp", module.get());
    StructType *complex_type = StructType::create(*context, "struct.Complex");
    complex_type->setBody({ Type::getDoubleTy(*context), Type::getDoubleTy(*context) });
    StructType *string_type = StructType::create(*context, "struct.String");
    string_type->setBody({ Type::getInt8PtrTy(*context), Type::getInt64Ty(*context) });
    FunctionType *dummy_type = FunctionType::get(builder.getVoidTy(), { PointerType::get(string_type, 0) }, false);
    Function::Create(dummy_type, Function::ExternalLinkage, "stringFunc", module.get());
    auto union_type = Type::getInt64Ty(*context); 
    auto enum_type = Type::getInt32Ty(*context);
    FunctionType *complex_math_type = FunctionType::get(builder.getVoidTy(), { PointerType::get(complex_type, 0), builder.getInt32Ty(), PointerType::get(complex_type, 0), PointerType::get(complex_type, 0) }, false);
    Function::Create(complex_math_type, Function::ExternalLinkage, "complexMath", module.get());
    StructType *abaci_value_type = StructType::create(*context, "struct.AbaciValue");
    abaci_value_type->setBody({ union_type, enum_type });
    StructType *environment_type = StructType::create(*context, "struct.Environment");
    FunctionType *print_value_type = FunctionType::get(builder.getVoidTy(), { PointerType::get(abaci_value_type, 0) }, false);
    Function::Create(print_value_type, Function::ExternalLinkage, "printValue", module.get());
    FunctionType *print_comma_type = FunctionType::get(builder.getVoidTy(), {}, false);
    Function::Create(print_comma_type, Function::ExternalLinkage, "printComma", module.get());
    FunctionType *print_ln_type = FunctionType::get(builder.getVoidTy(), {}, false);
    Function::Create(print_ln_type, Function::ExternalLinkage, "printLn", module.get());
    FunctionType *set_variable_type = FunctionType::get(builder.getVoidTy(), { PointerType::get(environment_type, 0), builder.getInt8PtrTy(), PointerType::get(abaci_value_type, 0), builder.getInt1Ty() }, false);
    Function::Create(set_variable_type, Function::ExternalLinkage, "setVariable", module.get());
    FunctionType *get_variable_type = FunctionType::get(PointerType::get(abaci_value_type, 0), { PointerType::get(environment_type, 0), builder.getInt8PtrTy() }, false);
    Function::Create(get_variable_type, Function::ExternalLinkage, "getVariable", module.get());
    FunctionType *begin_scope_type = FunctionType::get(builder.getVoidTy(), { PointerType::get(environment_type, 0) }, false);
    Function::Create(begin_scope_type, Function::ExternalLinkage, "beginScope", module.get());
    FunctionType *end_scope_type = FunctionType::get(builder.getVoidTy(), { PointerType::get(environment_type, 0) }, false);
    Function::Create(end_scope_type, Function::ExternalLinkage, "endScope", module.get());
    for (const auto& instantiation : cache->getInstantiations()) {
        std::string function_name{ mangled(instantiation.name, instantiation.parameter_types) };
        FunctionType *inst_func_type = FunctionType::get(builder.getVoidTy(), {}, false);
        current_function = Function::Create(inst_func_type, Function::ExternalLinkage, function_name, *module);
        BasicBlock *entry_block = BasicBlock::Create(*context, "entry", current_function);
        BasicBlock *exit_block = BasicBlock::Create(*context, "exit", current_function);
        builder.SetInsertPoint(entry_block);
        const auto& cache_function = cache->getFunction(instantiation.name);
        auto current_scope = environment->getCurrentDefineScope();
        environment->setCurrentDefineScope(instantiation.scope);
        StmtCodeGen stmt(*this, exit_block, instantiation.scope->getDepth());
        Value *environment_ptr = ConstantInt::get(builder.getInt64Ty(), reinterpret_cast<intptr_t>(environment));
        Value *typed_environment_ptr = builder.CreateBitCast(environment_ptr, PointerType::get(environment_type, 0));
        builder.CreateCall(module->getFunction("beginScope"), { typed_environment_ptr });
        for (const auto& function_stmt : cache_function.body) {
            stmt(function_stmt);
        }
        if (!dynamic_cast<const ReturnStmt*>(cache_function.body.back().get())) {
            builder.CreateBr(exit_block);
        }
        builder.SetInsertPoint(exit_block);
        builder.CreateCall(module->getFunction("endScope"), { typed_environment_ptr });
        builder.CreateRetVoid();
        environment->setCurrentDefineScope(current_scope);
    }
    FunctionType *main_func_type = FunctionType::get(builder.getVoidTy(), {}, false);
    current_function = Function::Create(main_func_type, Function::ExternalLinkage, function_name, module.get());
    BasicBlock *entry_block = BasicBlock::Create(*context, "entry", current_function);
    builder.SetInsertPoint(entry_block);
}

```

A call to `getExecFunction()` finalized the module and clears the cache of function instantiations. The return value of `jitBuilder.create()` is stored as a member variable so that it outlives this function call. LLVM requires that the `module` and `context` are "moved" into this JIT, along with all necessary defintions for the dynamic library. Successful lookup of the function name in the JIT allows return of its address cast to `StmtFunctionType`.

```cpp
StmtFunctionType JIT::getExecFunction() {
    builder.CreateRetVoid();
    cache->clearInstantiations();
    //verifyModule(*module, &errs());
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    jit = jitBuilder.create();
    if (!jit) {
        UnexpectedError("Failed to create LLJIT instance");
    }
    if (auto err = (*jit)->addIRModule(ThreadSafeModule(std::move(module), std::move(context)))) {
        handleAllErrors(std::move(err), [&](ErrorInfoBase& eib) {
            errs() << "Error: " << eib.message() << '\n';
        });
        UnexpectedError("Failed add IR module");
    }
    if (auto err = (*jit)->getMainJITDylib().define(
            llvm::orc::absoluteSymbols({{(*jit)->getExecutionSession().intern("pow"), { reinterpret_cast<uintptr_t>(&pow), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("strcmp"), { reinterpret_cast<uintptr_t>(&strcmp), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("memcpy"), { reinterpret_cast<uintptr_t>(&memcpy), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("complexMath"), { reinterpret_cast<uintptr_t>(&complexMath), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("printValue"), { reinterpret_cast<uintptr_t>(&printValue), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("printComma"), { reinterpret_cast<uintptr_t>(&printComma), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("printLn"), { reinterpret_cast<uintptr_t>(&printLn), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("setVariable"), { reinterpret_cast<uintptr_t>(&setVariable), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("getVariable"), { reinterpret_cast<uintptr_t>(&getVariable), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("beginScope"), { reinterpret_cast<uintptr_t>(&beginScope), JITSymbolFlags::Exported }},
            {(*jit)->getExecutionSession().intern("endScope"), { reinterpret_cast<uintptr_t>(&endScope), JITSymbolFlags::Exported }}
            }))) {
        handleAllErrors(std::move(err), [&](ErrorInfoBase& eib) {
            errs() << "Error: " << eib.message() << '\n';
        });
        UnexpectedError("Failed to add symbols to module");
    }
    auto func_symbol = (*jit)->lookup(function_name);
    if (!func_symbol) {
        UnexpectedError("JIT function not found");
    }
    return reinterpret_cast<StmtFunctionType>(func_symbol->getAddress());
}

}
```

## Compiler Driver

### `main.cpp`

The main program contained in this file is not intended to be fully-featured, but instead just provide an environment for interactively executing Abaci0 code, or running an Abaci0 script (extension `.abaci`) as a batch process.

In the case of a single program argument (`./abaci0 script.abaci`), the file is read in one go before `ast`, `environment` and `functions` are created (as empty). The call to `parse_block()` returns `true` if successful, causing use of class `TypeCodeGen` over the whole program. If no errors are caused by this stage, `jit` and `code_gen` are created and the resulting `programFunc` is called before the main program exits. Syntax errors cause the message `"Could not parse file."`, while other errors would result in exceptions being thrown. ERrors are caught and the main program exits with a non-zero failure code.

```cpp
#include "ast/Expr.hpp"
#include "ast/Stmt.hpp"
#include "parser/Parse.hpp"
#include "codegen/CodeGen.hpp"
#include "engine/JIT.hpp"
#include "utility/Utility.hpp"
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <iostream>
#include <fstream>
#include <string>
using fmt::print;

const std::string version = "0.8.0 (2024-Apr-09)";

int main(const int argc, const char **argv) {
    if (argc == 2) {
        std::ifstream input_file{ argv[1] };
        if (input_file) {
            std::string input_text;
            std::getline(input_file, input_text, '\0');
            abaci::ast::StmtList ast;
            abaci::utility::Environment environment;
            abaci::engine::Cache functions;
            try {
                if (abaci::parser::parse_block(input_text, ast)) {
                    abaci::codegen::TypeCodeGen type_code_gen(&environment, &functions);
                    for (const auto& stmt : ast) {
                        type_code_gen(stmt);
                    }
                    abaci::engine::JIT jit("Abaci", "main", &environment, &functions);
                    abaci::codegen::StmtCodeGen code_gen(jit);
                    for (const auto& stmt : ast) {
                        code_gen(stmt);
                    }
                    auto programFunc = jit.getExecFunction();
                    programFunc();
                    return 0;
                }
                else {
                    AbaciError("Could not parse file.");
                }
            }
            catch (AbaciError& error) {
                print(std::cout, "{}\n", error.what());
                return 1;
            }
        }
    }
```

Where the Abaci0 interpreter is run without arguments, a version string is output and it waits for user input at a `>` prompt. In the case of incomplete (or erroneous) input, a `?` prompt is output, entering a blank line forces an error message. If extra input was detected after a valid statement(s), it is printed (after the next `>` prompt) for completion. Invalid input results in the message `"Syntax error."` while other errors indicate problems with code generation. Entering a blank line at a `>` prompt exits the interpreter.

```cpp
    std::string input;
    print(std::cout, "Abaci0 version {}\nEnter code, or a blank line to end:\n> ", version);
    std::getline(std::cin, input);

    abaci::ast::StmtNode ast;
    abaci::utility::Environment environment;
    abaci::engine::Cache functions;
    while (!input.empty()) {
        std::string more_input = "\n";
        while (!abaci::parser::test_statement(input) && !more_input.empty()) {
            print(std::cout, "? ");
            std::getline(std::cin, more_input);
            input += '\n' + more_input;
        }
        if (abaci::parser::test_statement(input)) {
            while (!input.empty() && abaci::parser::parse_statement(input, ast)) {
                try {
                    abaci::codegen::TypeCodeGen type_code_gen(&environment, &functions);
                    type_code_gen(ast);
                    abaci::engine::JIT jit("Abaci", "main", &environment, &functions);
                    abaci::codegen::StmtCodeGen code_gen(jit);
                    code_gen(ast);
                    auto stmtFunc = jit.getExecFunction();
                    stmtFunc();
                }
                catch (AbaciError& error) {
                    print(std::cout, "{}\n", error.what());
                }
            }
            more_input = input;
        }
        else {
            print(std::cout, "{}\n", "Syntax error.");
            more_input.clear();
        }
        print(std::cout, "> {}", more_input);
        std::getline(std::cin, input);
        if (!more_input.empty()) {
            input = more_input + '\n' + input;
        }
    }
    return 0;
}
```

### `CMakeLists.txt`

A simple build script is provided, targeting Linux but which may be adaptable for other platforms. Currently only compilation with `g++` is supported.

```cmake
cmake_minimum_required(VERSION 3.25.1)
project(abaci0-literate)

# Compiler settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/utility)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/parser)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/codegen)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/engine)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Find fmt
find_package(fmt REQUIRED)

# Source files
set(SOURCES
    src/lib/Abaci.cpp
    src/utility/Utility.cpp
    src/utility/Environment.cpp
    src/parser/Parse.cpp
    src/codegen/ExprCodeGen.cpp
    src/codegen/StmtCodeGen.cpp
    src/codegen/TypeCodeGen.cpp
    src/engine/Cache.cpp
    src/engine/JIT.cpp
    src/main.cpp
)

# Executable
add_executable(abaci0 ${SOURCES})

# Link LLVM libraries
llvm_map_components_to_libnames(LLVM_LIBS core executionengine interpreter analysis native bitwriter orcjit)
target_link_libraries(abaci0 ${LLVM_LIBS} fmt::fmt)
```

Simple usage assuming llvm-dev, libboost-dev and libfmt packages are available:

```bash
//@
mkdir build && cd build
cmake ..
make
```
