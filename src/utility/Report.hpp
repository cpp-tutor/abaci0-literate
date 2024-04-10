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

#define LogicError(error_string) { throw AbaciError(error_string); }
#define UnexpectedError(error_string) { throw CompilerError(error_string, __FILE__, __LINE__); }
#define Assert(condition) { if (!(condition)) throw AssertError(#condition, __FILE__, __LINE__); }

#endif
