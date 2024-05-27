#ifndef Report_hpp
#define Report_hpp

#include <exception>
#include <string>
#include <fmt/format.h>
using fmt::format;
using fmt::runtime;

template<typename... Ts>
class AbaciError : public std::exception {
private:
    std::string message{};
public:
    AbaciError(const char *error_string, Ts... args) : message{ format(runtime(error_string), std::forward<Ts>(args)...) } {}
    virtual const char *what() const noexcept override { return message.c_str(); }
};

template<typename... Ts>
class CompilerError : public std::exception {
private:
    std::string message{};
public:
    CompilerError(const char* source_file, const int line_number, const char *error_string, Ts... args)
        : message{ format(runtime(error_string), std::forward<Ts>(args)...) }
    {
        message.append(" Compiler inconsistency detected!");
        if (source_file != std::string{}) {
            message.append("\nSource filename: ");
            message.append(source_file);
            if (line_number != -1) {
                message.append(", line: ");
                message.append(std::to_string(line_number));
            }
        }
    }
    virtual const char *what() const noexcept override { return message.c_str(); }
};

class AssertError : public std::exception {
private:
    std::string message{};
public:
    AssertError(const char* source_file, const int line_number, const char *assertion)
        : message{ format(runtime("Assertion failed: {}"), assertion) }
    {
        if (source_file != std::string{}) {
            message.append("\nSource filename: ");
            message.append(source_file);
            if (line_number != -1) {
                message.append(", Line number: ");
                message.append(std::to_string(line_number));
            }
        }
    }
    virtual const char *what() const noexcept override { return message.c_str(); }
};

#define LogicError0(error_string) throw AbaciError(error_string)
#define LogicError1(error_string, arg1) throw AbaciError(error_string, arg1)
#define LogicError2(error_string, arg1, arg2) throw AbaciError(error_string, arg1, arg2)
#define UnexpectedError0(error_string) throw CompilerError(__FILE__, __LINE__, error_string)
#define UnexpectedError1(error_string, arg1) throw CompilerError(__FILE__, __LINE__, error_string, arg1)
#define UnexpectedError2(error_string, arg1, arg2) throw CompilerError(__FILE__, __LINE__, error_string, arg1, arg2)
#define Assert(condition) if (!(condition)) throw AssertError(__FILE__, __LINE__, #condition)

#endif
