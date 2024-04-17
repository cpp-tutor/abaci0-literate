#include "Utility.hpp"
#include "Report.hpp"
#include "parser/Keywords.hpp"
#include <iomanip>
#include <charconv>

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

std::string mangled(const std::string& name, const std::vector<AbaciValue::Type>& types) {
    std::string function_name{ "_" };
    for (unsigned char ch : name) {
        if (ch == '\'') {
            function_name.push_back('.');
        }
        else if (ch >= 0x80) {
            function_name.push_back('.');
            char buffer[16];
            auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<int>(ch), 16);
            if (ec != std::errc()) {
                UnexpectedError("Bad numeric conversion.");
            }
            function_name.append(buffer);
        }
        else {
            function_name.push_back(ch);
        }
    }
    for (const auto parameter_type : types) {
        function_name.append("_");
        function_name.append(std::to_string(parameter_type));
    }
    return function_name;
}

} // namespace abaci::utility

std::ostream& operator<<(std::ostream& os, const abaci::utility::AbaciValue& value) {
    using abaci::utility::AbaciValue;
    switch (value.type) {
        case AbaciValue::Nil:
            return os << NIL;
        case AbaciValue::Boolean:
            return os << (value.value.boolean ? TRUE : FALSE);
        case AbaciValue::Integer:
            return os << static_cast<long long>(value.value.integer);
        case AbaciValue::Float:
            return os << std::setprecision(10) << value.value.floating;
        case AbaciValue::Complex:
            return os << value.value.complex->real << (value.value.complex->imag >= 0 ? "+" : "") << value.value.complex->imag << IMAGINARY;
        case AbaciValue::String:
            return os.write(reinterpret_cast<const char*>(value.value.str->ptr), value.value.str->len);
        default:
            return os << value.type << '?';
    }
}

std::ostream& operator<<(std::ostream& os, const abaci::utility::Operator op) {
    for (const auto& item : abaci::utility::Operators) {
        if (item.second == op) {
            return os << '(' << item.first << ')';
        }
    }
    return os << "(?)";
}
