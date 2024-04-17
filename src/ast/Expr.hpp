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

class ExprNode {
public:
    enum Association { Unset, Left, Right, Unary, Boolean };
    enum Type { ValueNode, OperatorNode, ListNode, VariableNode, CallNode };
    ExprNode() = default;
    ExprNode(const ExprNode&) = default;
    ExprNode& operator=(const ExprNode&) = default;
    template<typename T>
    explicit ExprNode(const T& data, Association association = Unset) : data{ data }, association{ association } {}
    template<typename T>
    ExprNode& operator=(const T& d) { data = d; association = Unset; return *this; }
    Association getAssociation() const { return association; }
    const auto& get() const { return data; }
private:
    std::variant<AbaciValue,Operator,ExprList,Variable,ValueCall> data;
    Association association{ Unset };
};

} // namespace abaci::ast

BOOST_FUSION_ADAPT_STRUCT(abaci::ast::ValueCall, name, args)

#endif
