#include "Abaci.hpp"
#include "utility/Report.hpp"
#include "parser/Keywords.hpp"
#include "parser/Messages.hpp"
#include <charconv>
#include <complex>
#include <string>
#include <cstring>
#include <cstdio>
#include <fmt/core.h>
#include <fmt/format.h>
using fmt::print;
using fmt::format;
using fmt::runtime;

using abaci::utility::AbaciValue;
using abaci::utility::Complex;
using abaci::utility::Operator;
using abaci::utility::String;
using abaci::utility::Environment;

void printValue(AbaciValue *value) {
    switch (value->type) {
        case AbaciValue::Nil:
            print("{}", NIL);
            break;
        case AbaciValue::Boolean:
            print("{}", value->value.boolean ? TRUE : FALSE);
            break;
        case AbaciValue::Integer:
            print("{}", static_cast<long long>(value->value.integer));
            break;
        case AbaciValue::Float:
            print("{:.10g}", value->value.floating);
            break;
        case AbaciValue::Complex:
            print("{:.10g}", value->value.complex->real);
            if (value->value.complex->imag != 0) {
                print("{:+.10g}{}", value->value.complex->imag, IMAGINARY);
            }
            break;
        case AbaciValue::String:
            fwrite(reinterpret_cast<const char*>(value->value.str->ptr), value->value.str->len, 1, stdout);
            break;
        case AbaciValue::Object:
            print(runtime(InstanceOf), reinterpret_cast<const char*>(value->value.object->class_name));
            break;
        default:
            UnexpectedError1(UnknownType, static_cast<int>(value->type));
            break;
    }
}

void printComma() {
    print("{}", ' ');
}

void printLn() {
    print("{}", '\n');
}

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
            UnexpectedError0(BadOperator);
    }
    result->real = r.real();
    result->imag = r.imag();
}

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

void setObjectData(Environment *environment, char *name, int *indices, AbaciValue *value) {
    auto data = (strcmp(name, "_this") == 0) ? environment->getThisPtr() : environment->getCurrentScope()->getValue(name);
    while (*indices != -1) {
        data = &data->value.object->variables[*indices];
        ++indices;
    }
    *data = *value;
}

AbaciValue *getObjectData(Environment *environment, char *name, int *indices) {
    auto data = (strcmp(name, "_this") == 0) ? environment->getThisPtr() : environment->getCurrentScope()->getValue(name);
    while (*indices != -1) {
        data = &data->value.object->variables[*indices];
        ++indices;
    }
    return data;
}

void beginScope(Environment *environment) {
    environment->beginScope();
}

void endScope(Environment *environment) {
    environment->endScope();
}

void setThisPtr(Environment *environment, AbaciValue *ptr) {
    environment->setThisPtr(ptr);
}

void unsetThisPtr(Environment *environment) {
    environment->unsetThisPtr();
}

void getUserInput(String *str) {
    fgets(reinterpret_cast<char*>(str->ptr), str->len, stdin);
    str->len = strlen(reinterpret_cast<char*>(str->ptr));
    if (*(str->ptr + str->len - 1) == '\n') {
        --str->len;
    }
    fflush(stdin);
}

void convertType(AbaciValue *to, AbaciValue *from) {
    switch (to->type) {
        case AbaciValue::Integer:
            switch(from->type) {
                case AbaciValue::Boolean:
                    to->value.integer = static_cast<long long>(from->value.boolean);
                    break;
                case AbaciValue::Integer:
                    to->value.integer = from->value.integer;
                    break;
                case AbaciValue::Float:
                    to->value.integer = static_cast<long long>(from->value.floating);
                    break;
                case AbaciValue::String: {
                    auto *str = reinterpret_cast<const char*>(from->value.str->ptr);
                    if (strncmp(HEX_PREFIX, str, strlen(HEX_PREFIX)) == 0) {
                        std::from_chars(str + strlen(HEX_PREFIX), str + from->value.str->len, to->value.integer, 16);
                    }
                    else if (strncmp(BIN_PREFIX, str, strlen(BIN_PREFIX)) == 0) {
                        std::from_chars(str + strlen(BIN_PREFIX), str + from->value.str->len, to->value.integer, 2);
                    }
                    else if (strncmp(OCT_PREFIX, str, strlen(OCT_PREFIX)) == 0) {
                        std::from_chars(str + strlen(OCT_PREFIX), str + from->value.str->len, to->value.integer, 8);
                    }
                    else {
                        std::from_chars(str, str + from->value.str->len, to->value.integer, 10);
                    }
                    break;
                }
                default:
                    LogicError1(BadConvType, INT);
            }
            break;
        case AbaciValue::Float:
            switch(from->type) {
                case AbaciValue::Boolean:
                    to->value.floating = static_cast<double>(from->value.boolean);
                    break;
                case AbaciValue::Integer:
                    to->value.floating = static_cast<double>(from->value.integer);
                    break;
                case AbaciValue::Float:
                    to->value.floating = from->value.floating;
                    break;
                case AbaciValue::String: {
                    auto *str = reinterpret_cast<const char*>(from->value.str->ptr);
                    std::from_chars(str, str + from->value.str->len, to->value.floating);
                    break;
                }
                default:
                    LogicError1(BadConvType, FLOAT);
            }
            break;
        case AbaciValue::Complex:
            switch(from->type) {
                case AbaciValue::Boolean:
                    to->value.complex->real = static_cast<double>(from->value.boolean);
                    to->value.complex->imag = 0.0;
                    break;
                case AbaciValue::Integer:
                    to->value.complex->real = static_cast<double>(from->value.integer);
                    to->value.complex->imag = 0.0;
                    break;
                case AbaciValue::Float:
                    to->value.complex->real = from->value.floating;
                    to->value.complex->imag = 0.0;
                    break;
                case AbaciValue::String: {
                    auto *str = reinterpret_cast<const char*>(from->value.str->ptr);
                    double d;
                    auto [ ptr, ec ] = std::from_chars(str, str + from->value.str->len, d);
                    if (strncmp(ptr, IMAGINARY, strlen(IMAGINARY)) == 0) {
                        to->value.complex->real = 0;
                        to->value.complex->imag = d;
                    }
                    else if (*ptr) {
                        to->value.complex->real = d;
                        if (*ptr == '+') {
                            ++ptr;
                        }
                        std::from_chars(ptr, str + from->value.str->len, d);
                        to->value.complex->imag = d;
                    }
                    else {
                        to->value.complex->real = d;
                        to->value.complex->imag = 0;
                    }
                    break;
                }
                case AbaciValue::Complex:
                    to->value.complex->real = from->value.complex->real;
                    to->value.complex->imag = from->value.complex->imag;
                    break;
                default:
                    LogicError1(BadConvType, COMPLEX);
            }
            break;
        case AbaciValue::String: {
            std::string str;
            switch (from->type) {
                case AbaciValue::Boolean:
                    str = format("{}", from->value.boolean ? TRUE : FALSE);
                    break;
                case AbaciValue::Integer:
                    str = format("{}", static_cast<long long>(from->value.integer));
                    break;
                case AbaciValue::Float:
                    str = format("{:.10g}", from->value.floating);
                    break;
                case AbaciValue::Complex:
                    str = format("{:.10g}", from->value.complex->real);
                    if (from->value.complex->imag != 0) {
                        str.append(format("{:+.10g}{}", from->value.complex->imag, IMAGINARY));
                    }
                    break;
                case AbaciValue::String:
                    str.assign(reinterpret_cast<const char*>(from->value.str->ptr), from->value.str->len);
                    break;
                default:
                    LogicError1(BadConvType, STR);
            }
            char *ptr = reinterpret_cast<char*>(to->value.str->ptr);
            if (str.size() < to->value.str->len) {
                to->value.str->len = str.size();
            }
            strncpy(ptr, str.c_str(), to->value.str->len);
            break;
        }
        case AbaciValue::Real:
            switch(from->type) {
                case AbaciValue::Complex:
                    to->value.floating = from->value.complex->real;
                    to->type = AbaciValue::Float;
                    break;
                default:
                    LogicError1(NeedType, COMPLEX);
            }
            break;
        case AbaciValue::Imaginary:
            switch(from->type) {
                case AbaciValue::Complex:
                    to->value.floating = from->value.complex->imag;
                    to->type = AbaciValue::Float;
                    break;
                default:
                    LogicError1(NeedType, COMPLEX);
            }
            break;
        default:
            UnexpectedError1(BadConvTarget, static_cast<int>(to->type));
    }
}
