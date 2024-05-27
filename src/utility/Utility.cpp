#include "Utility.hpp"
#include "Report.hpp"
#include "parser/Keywords.hpp"

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
    { FROM, Operator::From },
    { TO, Operator::To }
};

const std::unordered_map<std::string,AbaciValue::Type> TypeConversions{
    { INT, AbaciValue::Integer },
    { FLOAT, AbaciValue::Float },
    { COMPLEX, AbaciValue::Complex },
    { STR, AbaciValue::String },
    { REAL, AbaciValue::Real },
    { IMAG, AbaciValue::Imaginary }
};

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
            value.object = rhs.value.object ? new abaci::utility::Object(rhs.value.object->class_name, rhs.value.object->variables_sz, rhs.value.object->variables) : nullptr;
            break;
        default:
            break;
    }
    type = rhs.type;
}

Object::Object(const char8_t *s, std::size_t sz, AbaciValue *data) {
    class_name = new char8_t[strlen(reinterpret_cast<const char*>(s)) + 1];
    strcpy(reinterpret_cast<char*>(class_name), reinterpret_cast<const char*>(s));
    variables_sz = sz;
    variables = new AbaciValue[variables_sz];
    for (std::size_t i = 0; i != variables_sz; ++i) {
        variables[i] = AbaciValue(data[i]);
    }
}

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

Object::~Object() {
    for (std::size_t i = 0; i != variables_sz; ++i) {
        variables[i].~AbaciValue();
    }
    delete[] variables;
    variables_sz = 0;
    delete[] class_name;
    class_name = nullptr;
}

} // namespace abaci::utility
