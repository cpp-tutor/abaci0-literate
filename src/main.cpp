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

const std::string version = "0.8.2 (2024-Apr-17)";

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
