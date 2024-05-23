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

struct Object {
    char8_t *class_name;
    std::size_t variables_sz;
    AbaciValue *variables;
    Object(const char8_t *s, std::size_t sz, AbaciValue *data);
    ~Object();
};

class Variable {
    std::string name;
public:
    Variable() = default;
    Variable(const Variable&) = default;
    Variable& operator=(const Variable&) = default;
    Variable(const std::string& name) : name{ name } {}
    const auto& get() const { return name; }
    bool operator==(const Variable& rhs) const { return name == rhs.name; }
};

struct AbaciValue {
    enum Type { Nil, Boolean, Integer, Float, Complex, String, Object, TypeMask = 15, Constant = 16, Unset = 127 };
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
    explicit AbaciValue(const std::string& s) : type{ String } { value.str = new abaci::utility::String(s); }
    explicit AbaciValue(const char8_t *s, std::size_t sz, AbaciValue *data) : type{ Object } { value.object = new abaci::utility::Object(s, sz, data); }
    AbaciValue(const AbaciValue& rhs) { clone(rhs); }
    AbaciValue& operator=(const AbaciValue& rhs) { if (this != &rhs) { this->~AbaciValue(); clone(rhs); } return *this; }
    ~AbaciValue();
private:
    void clone(const AbaciValue&);
};

static_assert(sizeof(AbaciValue::value) == 8, "AbaciValue::value must be exactly 64 bits");

enum class Operator { None, Plus, Minus, Times, Divide, Modulo, FloorDivide, Exponent,
    Equal, NotEqual, Less, LessEqual, GreaterEqual, Greater,
    Not, And, Or, Compl, BitAnd, BitOr, BitXor,
    Comma, SemiColon, From, To };

extern const std::unordered_map<std::string,Operator> Operators;

} // namespace abaci::utility

std::ostream& operator<<(std::ostream& os, const abaci::utility::AbaciValue&);

std::ostream& operator<<(std::ostream& os, const abaci::utility::Operator);

template<typename T>
std::string toString(const T& value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

#endif
