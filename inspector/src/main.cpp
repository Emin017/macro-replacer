#include <iostream>
#include <string>
#include <utility>

#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/types/Type.h"
#include "slang/driver/Driver.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxVisitor.h"
#include "slang/text/SourceManager.h"
#include "slang/diagnostics/TextDiagnosticClient.h"
#include "slang/diagnostics/DiagnosticEngine.h"

#include "slang/ast/Expression.h"
#include "slang/ast/expressions/AssertionExpr.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/AssertionExpr.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include <set>

using namespace slang;
using namespace slang::driver;
using namespace slang::ast;
using namespace slang::syntax;

// Help function: Convert ArgumentDirection to string
std::string directionToString(ArgumentDirection dir) {
    switch (dir) {
        case ArgumentDirection::In:
            return "Input";
        case ArgumentDirection::Out:
            return "Output";
        case ArgumentDirection::InOut:
            return "Inout";
        case ArgumentDirection::Ref:
            return "Ref";
        default:
            return "Unknown";
    }
}

// ==========================================
// Helper: Infer width from expression
// ==========================================
std::string inferWidth(const Expression& expr, const Scope& scope) {
    const Type& type = *expr.type;
    auto width = type.getBitWidth();

    if (width > 0) {
        return std::to_string(width);
    }

    if (type.toString() != "<error>") {
        return std::to_string(width);
    }

    // For error types, try to infer from expression kind
    const Expression* targetExpr = &expr;

    // If this is an InvalidExpression, try to unwrap it
    if (expr.kind == ExpressionKind::Invalid) {
        const auto& invalidExpr = expr.as<InvalidExpression>();
        if (invalidExpr.child) {
            targetExpr = invalidExpr.child;
        }
    }

    // Check if this is a RangeSelect expression (e.g., dat_i[63:0])
    if (targetExpr->kind == ExpressionKind::RangeSelect) {
        const auto& rangeExpr = targetExpr->as<RangeSelectExpression>();

        // Try to get width from the type first
        auto rangeWidth = rangeExpr.type->getBitWidth();
        if (rangeWidth > 0) {
            return std::to_string(rangeWidth) + " (inferred from slice)";
        }

        // Try to calculate width from left and right bounds
        auto& comp = scope.getCompilation();
        EvalContext evalCtx(comp.getRoot());

        auto leftVal = rangeExpr.left().eval(evalCtx);
        auto rightVal = rangeExpr.right().eval(evalCtx);

        if (leftVal.isInteger() && rightVal.isInteger()) {
            int64_t left = leftVal.integer().as<int64_t>().value_or(0);
            int64_t right = rightVal.integer().as<int64_t>().value_or(0);
            int64_t calculatedWidth = std::abs(left - right) + 1;
            return std::to_string(calculatedWidth) + " (calculated from [" 
                   + std::to_string(left) + ":" + std::to_string(right) + "])";
        }
        return "(unable to evaluate slice bounds)";
    }

    // Check if this is a NamedValue expression (e.g., addr_i)
    if (targetExpr->kind == ExpressionKind::NamedValue) {
        const auto& namedExpr = targetExpr->as<NamedValueExpression>();
        const ValueSymbol& symbol = namedExpr.symbol;
        const auto& type = symbol.getType();
        auto width = type.getBitWidth();

        if (width > 0) {
            return std::to_string(width) + " (inferred from symbol '" + std::string(symbol.name) + "')";
        } else {
             return "(NamedValue symbol '" + std::string(symbol.name) + "' type: " + type.toString() + ")";
        }
    }

    return "(type error, expression kind: " + std::string(toString(targetExpr->kind)) + ")";
}

// ==========================================
// Find Module Definition in AST
// ==========================================
bool findModuleInAST(Compilation& compilation, const std::string& targetName) {
    const RootSymbol& root = compilation.getRoot();

    for (auto instance : root.topInstances) {
        if (instance->name == targetName) {
            std::cout << "[Result] Found Definition for '" << targetName << "'" << '\n';
            std::cout << "--------------------------------------------" << '\n';
            std::cout << "Source: Full Module Definition (AST)" << '\n';

            const InstanceBodySymbol& body = instance->body;
            for (auto& member : body.members()) {
                if (member.kind == SymbolKind::Port) {
                    const auto& port = member.as<PortSymbol>();
                    std::cout << "  Port: " << port.name;
                    std::cout << " | Dir: " << directionToString(port.direction);
                    std::cout << " | Type: " << port.getType().toString();
                    std::cout << '\n';
                }
            }
            std::cout << "--------------------------------------------" << '\n';
            return true;
        }
    }
    return false;
}

// ==========================================
// Recursive AST Visitor for Instantiations
// ==========================================
void findInstantiationsInAST(const Scope& scope, const std::string& targetName, bool& foundAny, std::set<std::string>& visited) {
    for (auto& member : scope.members()) {
        if (member.kind == SymbolKind::Instance) {
            const auto& instance = member.as<InstanceSymbol>();

            std::string hierPath = instance.getHierarchicalPath();

            if (visited.count(hierPath)) continue;
            visited.insert(hierPath);

            // Check if definition matches target
            if (instance.getDefinition().name == targetName) {
                foundAny = true;
                std::cout << "[Result] Found Instantiation (AST) for '" << targetName << "'" << '\n';
                std::cout << "  Instance Name: " << instance.name << '\n';
                std::cout << "  Full Path: " << hierPath << '\n';
                std::cout << "--------------------------------------------" << '\n';
                std::cout << "Source: AST Connection Analysis" << '\n';

                // instance.resolvePortConnections(); // Private API, unnecessary as getPortConnections returns them
                for (auto conn : instance.getPortConnections()) {
                    std::cout << "  Port: " << conn->port.name;

                    const Expression* expr = conn->getExpression();
                    if (expr) {
                        const Type& type = *expr->type;
                        std::cout << " | Connected Signal Type: " << type.toString();
                        std::cout << " | Width: " << type.getBitWidth();
                    } else {
                        std::cout << " | Unconnected/Unknown";
                    }
                    std::cout << '\n';
                }
                std::cout << "--------------------------------------------" << '\n';
            }

            // Recurse into this instance's body to find nested instantiations
            findInstantiationsInAST(instance.body, targetName, foundAny, visited);
        }
        else if (member.kind == SymbolKind::UninstantiatedDef) {
            // Handle blackbox/uninstantiated modules
            const auto& uninst = member.as<UninstantiatedDefSymbol>();

            std::string hierPath = uninst.getHierarchicalPath();

            if (visited.count(hierPath)) continue;
            visited.insert(hierPath);

            if (uninst.definitionName == targetName) {
                foundAny = true;
                std::cout << "[Result] Found Instantiation (AST) for '" << targetName << "'" << '\n';
                std::cout << "  Instance Name: " << uninst.name << '\n';
                std::cout << "  Full Path: " << hierPath << '\n';
                std::cout << "--------------------------------------------" << '\n';
                std::cout << "Source: AST Connection Analysis (Blackbox)" << '\n' << std::flush;

                auto portNames = uninst.getPortNames();
                auto portExprs = uninst.getPortConnections();

                for (size_t i = 0; i < portExprs.size(); i++) {
                    if (i < portNames.size() && !portNames[i].empty()) {
                        std::cout << "  Port: " << portNames[i];
                    } else {
                        std::cout << "  Port: [Positional #" << i << "]";
                    }

                    if (portExprs[i] && portExprs[i]->kind == AssertionExprKind::Simple) {
                        // Extract the Expression from SimpleAssertionExpr
                        const auto& simpleExpr = portExprs[i]->as<SimpleAssertionExpr>();
                        const Expression& expr = simpleExpr.expr;
                        const Type& type = *expr.type;

                        std::cout << " | Connected Signal Type: " << type.toString();
                        std::cout << " | Width: " << inferWidth(expr, scope);
                    } else if (portExprs[i]) {
                        std::cout << " | Unconnected";
                    }
                    std::cout << '\n' << std::flush;
                }
                std::cout << "--------------------------------------------" << '\n' << std::flush;
            }
        }
        else if (member.isScope()) {
             // Recurse into other scopes (like generate blocks)
             findInstantiationsInAST(member.as<Scope>(), targetName, foundAny, visited);
        }
    }
}



int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <verilog_file> <module_name>" << '\n';
        return 1;
    }

    std::string targetModuleName = argv[2];
    SourceManager sourceManager;

    auto treeResult = SyntaxTree::fromFile(argv[1], sourceManager);
    if (!treeResult) {
        std::cerr << "Error loading file." << '\n';
        return 1;
    }
    const std::shared_ptr<SyntaxTree>& tree = *treeResult;

    // Try AST lookup first
    CompilationOptions options;
    // Don't force targetModuleName as top, because we want to find where it is instantiated.
    // If we force it, Slang might fail to elaborate the parent module if the target is a blackbox.
    // options.topModules.emplace(targetModuleName);

    Compilation compilation(options);
    compilation.addSyntaxTree(tree);

    bool foundInAST = findModuleInAST(compilation, targetModuleName);

    if (foundInAST) {
        return 0;
    }

    // Try AST Instantiation Search
    // This allows us to see the types and widths of signals connected to the instance.
    std::cout << "Searching for instantiations in AST..." << '\n';
    bool foundInstances = false;
    std::set<std::string> visited;
    findInstantiationsInAST(compilation.getRoot(), targetModuleName, foundInstances, visited);

    if (foundInstances) {
        return 0;
    }

    std::cout << "AST search yielded no results." << '\n';
    return 1;
}