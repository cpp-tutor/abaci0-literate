#include "Parse.hpp"
#include "Keywords.hpp"
#include "Messages.hpp"
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
using abaci::utility::TypeConversions;

using x3::char_;
using x3::string;
using x3::lit;
using x3::lexeme;
using x3::ascii::space;

using abaci::ast::ExprNode;
using abaci::ast::ExprList;
using abaci::ast::ValueCall;
using abaci::ast::DataCall;
using abaci::ast::MethodValueCall;
using abaci::ast::UserInput;
using abaci::ast::TypeConvItems;
using abaci::ast::TypeConv;
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
using abaci::ast::DataAssignStmt;
using abaci::ast::MethodCall;
using abaci::ast::ExpressionStmt;

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
x3::rule<class from, Operator> const from;
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
x3::rule<class this_ptr, Variable> const this_ptr;
x3::rule<class function_value_call, ValueCall> const function_value_call;
x3::rule<class data_value_call, DataCall> const data_value_call;
x3::rule<class this_value_call, DataCall> const this_value_call;
x3::rule<class data_method_call, MethodValueCall> const data_method_call;
x3::rule<class this_method_call, MethodValueCall> const this_method_call;
x3::rule<class user_input, UserInput> const user_input;
x3::rule<class type_conversion_items, TypeConvItems> const type_conversion_items;
x3::rule<class type_conversion, TypeConv> const type_conversion;

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
x3::rule<class data_assign_items, DataAssignStmt> const data_assign_items;
x3::rule<class data_assign_stmt, StmtNode> const data_assign_stmt;
x3::rule<class method_call_items, MethodCall> const method_call_items;
x3::rule<class this_call_items, MethodCall> const this_call_items;
x3::rule<class method_call, StmtNode> const method_call;
x3::rule<class this_assign_items, DataAssignStmt> const this_assign_items;
x3::rule<class expression_stmt_items, ExpressionStmt> const expression_stmt_items;
x3::rule<class expression_stmt, StmtNode> const expression_stmt;
x3::rule<class keywords, std::string> const keywords;
x3::rule<class statment, StmtNode> const statement;
x3::rule<class block, StmtList> const block;

auto makeNumber = [](auto& ctx) {
    const auto& str = _attr(ctx);
    if (str.find(IMAGINARY) != std::string::npos) {
        double imag;
        std::from_chars(str.data(), str.data() + str.size() - 1, imag);
        _val(ctx) = AbaciValue(0, imag);
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

auto makeBoolean = [](auto& ctx) {
    const auto& str = _attr(ctx);
    _val(ctx) = (str == NIL) ? AbaciValue() : AbaciValue((str == TRUE) ? true : false);
};

auto makeString = [](auto& ctx) {
    const auto& str = _attr(ctx);
    _val(ctx) = AbaciValue(str);
};

auto makeVariable = [](auto& ctx){
    const std::string str = _attr(ctx);
    _val(ctx) = Variable(str);
};

auto makeThisPtr = [](auto& ctx){
    _val(ctx) = Variable("_this");
};

auto makeConversion = [](auto& ctx){
    const TypeConvItems& items = _attr(ctx);
    auto iter = TypeConversions.find(items.to_type);
    if (iter != TypeConversions.end()) {
        _val(ctx) = TypeConv{ iter->second, std::shared_ptr<ExprNode>{ new ExprNode(items.expression) } };
    }
};

template<size_t Ty = ExprNode::Unset>
struct MakeNode {
    template<typename Context>
    void operator()(Context& ctx) {
        _val(ctx) = ExprNode(_attr(ctx), static_cast<ExprNode::Association>(Ty));
    }
};

template<typename T>
struct MakeStmt {
    template<typename Context>
    void operator()(Context& ctx) {
        _val(ctx) = StmtNode(new T(std::move(_attr(ctx))));
    }
};

auto getOperator = [](auto& ctx){
    const std::string str = _attr(ctx);
    const auto iter = Operators.find(str);
    if (iter != Operators.end()) {
        _val(ctx) = iter->second;
    }
    else {
        UnexpectedError0(BadOperator);
    }
};

const auto number_str_def = lexeme[+char_('0', '9') >> -( string(DOT) >> +char_('0', '9') ) >> -string(IMAGINARY)];
const auto base_number_str_def = lexeme[string(HEX_PREFIX) >> +( char_('0', '9') | char_('A', 'F') | char_('a', 'f') )]
    | lexeme[string(BIN_PREFIX) >> +char_('0', '1')]
    | lexeme[string(OCT_PREFIX) >> +char_('0', '7')];
const auto boolean_str_def = string(NIL) | string(FALSE) | string(TRUE);
const auto string_str_def = lexeme['"' >> *(char_ - '"') >> '"'];
const auto value_def = base_number_str[makeBaseNumber] | number_str[makeNumber] | boolean_str[makeBoolean] | string_str[makeString];

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
const auto from_def = string(FROM)[getOperator];
const auto to_def = string(TO)[getOperator];

const auto identifier_def = lexeme[( ( char_('A', 'Z') | char_('a', 'z') | char_('\'') | char_('\200', '\377') )
    >> *( char_('A', 'Z') | char_('a', 'z') | char_('0', '9') | char_('_') | char_('\'') | char_('\200', '\377') ) ) - keywords];
const auto variable_def = identifier[makeVariable];
const auto this_ptr_def = lit(THIS)[makeThisPtr];
const auto function_value_call_def = identifier >> call_args;
const auto data_value_call_def = variable >> +( DOT >> variable );
const auto this_value_call_def = this_ptr >> +( DOT >> variable );
const auto data_method_call_def = variable >> DOT >> *( variable >> DOT ) >> identifier >> call_args;
const auto this_method_call_def = this_ptr >> DOT >> *( variable >> DOT ) >> identifier >> call_args;
const auto user_input_def = lit(INPUT);
const auto type_conversion_items_def = ( string(INT) | string(FLOAT) | string(COMPLEX) | string(STR)
    | string(REAL) | string(IMAG) ) >> LEFT_PAREN >> expression >> RIGHT_PAREN;
const auto type_conversion_def = type_conversion_items[makeConversion];

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
const auto primary_n_def = value[MakeNode<>()] | LEFT_PAREN >> logic_or[MakeNode<ExprNode::Boolean>()] >> RIGHT_PAREN
    | type_conversion[MakeNode<>()] | function_value_call[MakeNode<>()] | this_method_call[MakeNode<>()] | data_method_call[MakeNode<>()] 
    | this_value_call[MakeNode<>()] | data_value_call[MakeNode<>()] | user_input[MakeNode<>()] | variable[MakeNode<>()];

const auto keywords_def = lit(AND) | CASE | CLASS | COMPLEX | ELSE | ENDCASE | ENDCLASS | ENDFN | ENDIF | ENDWHILE
    | FALSE | FLOAT | FN | IF | IMAG | INPUT | INT | LET | NIL | NOT | OR | PRINT | REAL | REPEAT | RETURN | STR | THIS | TRUE | UNTIL | WHEN | WHILE;

const auto comment_items_def = *(char_ - '\n');
const auto comment_def = REM >> comment_items[MakeStmt<CommentStmt>()];
const auto print_items_def = expression >> -( +comma | semicolon );
const auto print_stmt_def = PRINT >> print_items[MakeStmt<PrintStmt>()];
const auto let_items_def = variable >> (equal | from) >> expression;
const auto let_stmt_def = LET >> let_items[MakeStmt<InitStmt>()];
const auto assign_items_def = variable >> from >> expression;
const auto assign_stmt_def = assign_items[MakeStmt<AssignStmt>()];

const auto if_items_def = expression >> block >> -( ELSE >> block );
const auto if_stmt_def = IF >> if_items[MakeStmt<IfStmt>()] >> ENDIF;

const auto while_items_def = expression >> block;
const auto while_stmt_def = WHILE >> while_items[MakeStmt<WhileStmt>()] >> ENDWHILE;
const auto repeat_items_def = block >> UNTIL >> expression;
const auto repeat_stmt_def = REPEAT >> repeat_items[MakeStmt<RepeatStmt>()];

const auto when_items_def = WHEN >> expression >> block;
const auto case_items_def = expression >> *when_items >> -( ELSE >> block );
const auto case_stmt_def = CASE >> case_items[MakeStmt<CaseStmt>()] >> ENDCASE;

const auto function_parameters_def = LEFT_PAREN >> -( variable >> *( COMMA >> variable ) ) >> RIGHT_PAREN;
const auto function_items_def = identifier >> function_parameters >> block;
const auto function_def = FN >> function_items[MakeStmt<Function>()] >> ENDFN;
const auto expression_function_items_def = identifier >> function_parameters >> to >> expression;
const auto expression_function_def = LET >> expression_function_items[MakeStmt<ExprFunction>()];

const auto call_args_def = LEFT_PAREN >> -( expression >> *( COMMA >> expression) ) >> RIGHT_PAREN;
const auto call_items_def = identifier >> call_args;
const auto function_call_def = call_items[MakeStmt<FunctionCall>()];
const auto return_items_def = expression;
const auto return_stmt_def = RETURN >> return_items[MakeStmt<ReturnStmt>()];

const auto class_items_def = identifier >> function_parameters >> *(FN >> function_items >> ENDFN);
const auto class_template_def = CLASS >> class_items[MakeStmt<Class>()] >> ENDCLASS;
const auto data_assign_items_def = variable >> +( DOT >> variable ) >> from >> expression;
const auto this_assign_items_def = this_ptr >> +( DOT >> variable ) >> from >> expression;
const auto data_assign_stmt_def = this_assign_items[MakeStmt<DataAssignStmt>()] | data_assign_items[MakeStmt<DataAssignStmt>()];
const auto this_call_items_def = this_ptr >> DOT >> *( variable >> DOT ) >> identifier >> call_args;
const auto method_call_items_def = variable >> DOT >> *( variable >> DOT ) >> identifier >> call_args;
const auto method_call_def = this_call_items[MakeStmt<MethodCall>()] | method_call_items[MakeStmt<MethodCall>()];

const auto expression_stmt_items_def = expression;
const auto expression_stmt_def = expression_stmt_items[MakeStmt<ExpressionStmt>()];

const auto statement_def = if_stmt | print_stmt | expression_function | let_stmt | method_call | data_assign_stmt | assign_stmt | while_stmt | repeat_stmt | case_stmt | function | function_call | return_stmt | class_template | expression_stmt | comment;
const auto block_def = *statement;

BOOST_SPIRIT_DEFINE(number_str, base_number_str, boolean_str, string_str, value)
BOOST_SPIRIT_DEFINE(plus, minus, times, divide, modulo, floor_divide, exponent,
    equal, not_equal, less, less_equal, greater_equal, greater,
    logical_and, logical_or, logical_not, bitwise_and, bitwise_or, bitwise_xor, bitwise_compl,
    comma, semicolon, from, to)
BOOST_SPIRIT_DEFINE(expression, logic_or, logic_and, logic_and_n,
    bit_or, bit_or_n, bit_xor, bit_xor_n, bit_and, bit_and_n,
    equality, equality_n, comparison, comparison_n,
    term, term_n, factor, factor_n, unary, unary_n, index, index_n, primary_n)
BOOST_SPIRIT_DEFINE(identifier, variable, function_value_call, data_value_call, this_value_call,
    data_method_call, this_method_call, user_input, type_conversion_items, type_conversion, keywords,
    comment_items, comment, print_items, print_stmt,
    let_items, let_stmt, assign_items, assign_stmt, if_items, if_stmt,
    when_items, while_items, while_stmt, repeat_items, repeat_stmt, case_items, case_stmt,
    function_parameters, function_items, function, call_args, call_items, function_call,
    expression_function_items, expression_function,
    return_items, return_stmt, class_items, class_template, data_assign_items, data_assign_stmt,
    this_ptr, this_assign_items, this_call_items,
    method_call_items, method_call, expression_stmt_items, expression_stmt, statement, block)

bool parse_block(const std::string& block_str, StmtList& ast) {
    auto iter = block_str.begin();
    auto end = block_str.end();
    return phrase_parse(iter, end, block, space, ast);
}

bool parse_statement(std::string& stmt_str, StmtNode& ast) {
    auto iter = stmt_str.begin();
    auto end = stmt_str.end();
    bool result = phrase_parse(iter, end, statement, space, ast);
    stmt_str = std::string(iter, end);
    return result;
}

bool test_statement(const std::string& stmt_str) {
    auto iter = stmt_str.begin();
    auto end = stmt_str.end();
    return phrase_parse(iter, end, statement, space);
}

} // namespace abaci::parser
