#ifndef Expr_hpp
#define Expr_hpp

#include "utility/Utility.hpp"
#include <boost/fusion/adapted/struct.hpp>
#include <vector>
#include <variant>
#include <utility>
#include <memory>

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

struct DataCall {
    Variable name;
    std::vector<Variable> member_list;
};

struct MethodValueCall {
    Variable name;
    std::vector<Variable> member_list;
    std::string method;
    ExprList args;
};

struct UserInput {
    inline const static int MAX_SIZE = 256;
    std::string dummy;
};

struct TypeConv {
    inline const static int MAX_SIZE = 32;
    AbaciValue::Type to_type;
    std::shared_ptr<ExprNode> expression;
};

class ExprNode {
public:
    enum Association { Unset, Left, Right, Unary, Boolean };
    enum Type { ValueNode, OperatorNode, ListNode, VariableNode, CallNode, DataNode, MethodNode, InputNode, ConvNode };
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
    std::variant<AbaciValue,Operator,ExprList,Variable,ValueCall,DataCall,MethodValueCall,UserInput,TypeConv> data;
    Association association{ Unset };
};

struct TypeConvItems {
    std::string to_type;
    ExprNode expression;
};

} // namespace abaci::ast

BOOST_FUSION_ADAPT_STRUCT(abaci::ast::ValueCall, name, args)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::DataCall, name, member_list)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::MethodValueCall, name, member_list, method, args)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::UserInput, dummy)
BOOST_FUSION_ADAPT_STRUCT(abaci::ast::TypeConvItems, to_type, expression)

#endif
