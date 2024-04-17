#include "Abaci.hpp"
#include "utility/Report.hpp"
#include "parser/Keywords.hpp"
#include <complex>
#include <cstring>
#include <algorithm>
#include <fmt/core.h>
#include <fmt/format.h>
using fmt::print;

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
        default:
            print("{}?", static_cast<int>(value->type));
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
            UnexpectedError("Unknown operator.");
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

void beginScope(Environment *environment) {
    environment->beginScope();
}

void endScope(Environment *environment) {
    environment->endScope();
}
