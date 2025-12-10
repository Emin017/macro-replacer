#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/Expression.h"
#include "slang/ast/expressions/AssertionExpr.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/Type.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/diagnostics/TextDiagnosticClient.h"
#include "slang/driver/Driver.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxVisitor.h"
#include "slang/text/SourceManager.h"

using namespace slang;
using namespace slang::driver;
using namespace slang::ast;
using namespace slang::syntax;
using json = nlohmann::json;

// ==========================================
// Data Structures for Results
// ==========================================

struct PortInfo {
    std::string name;
    std::string direction;
    std::string type;
};

struct DefinitionInfo {
    std::string name;
    std::vector<PortInfo> ports;
};

struct ConnectionInfo {
    std::string portName;
    std::string signalType;
    std::string width;
    bool isConnected;
};

struct InstanceInfo {
    std::string instanceName;
    std::string fullPath;
    std::string definitionName;
    std::vector<ConnectionInfo> connections;
};

struct InspectorResult {
    std::optional<DefinitionInfo> definition;
    std::vector<InstanceInfo> instances;
};

// JSON Serialization
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PortInfo, name, direction, type)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DefinitionInfo, name, ports)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ConnectionInfo, portName, signalType, width, isConnected)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(InstanceInfo, instanceName, fullPath, definitionName,
                                   connections)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(InspectorResult, definition, instances)

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
            return std::to_string(calculatedWidth) + " (calculated from [" + std::to_string(left) +
                   ":" + std::to_string(right) + "])";
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
            return std::to_string(width) + " (inferred from symbol '" + std::string(symbol.name) +
                   "')";
        }
        else {
            return "(NamedValue symbol '" + std::string(symbol.name) +
                   "' type: " + type.toString() + ")";
        }
    }

    return "(type error, expression kind: " + std::string(toString(targetExpr->kind)) + ")";
}

// ==========================================
// Collect Module Definition
// ==========================================
void collectModuleInAST(Compilation& compilation, const std::string& targetName,
                        InspectorResult& result) {
    const RootSymbol& root = compilation.getRoot();

    for (auto instance : root.topInstances) {
        if (instance->name == targetName) {
            DefinitionInfo defInfo;
            defInfo.name = targetName;

            const InstanceBodySymbol& body = instance->body;
            for (auto& member : body.members()) {
                if (member.kind == SymbolKind::Port) {
                    const auto& port = member.as<PortSymbol>();
                    PortInfo portInfo;
                    portInfo.name = std::string(port.name);
                    portInfo.direction = directionToString(port.direction);
                    portInfo.type = port.getType().toString();
                    defInfo.ports.push_back(portInfo);
                }
            }
            result.definition = defInfo;
            return; // Found definition
        }
    }
}

// ==========================================
// Recursive AST Visitor for Instantiations
// ==========================================
void collectInstantiationsInAST(const Scope& scope, const std::string& targetName,
                                InspectorResult& result, std::set<std::string>& visited) {
    for (auto& member : scope.members()) {
        if (member.kind == SymbolKind::Instance) {
            const auto& instance = member.as<InstanceSymbol>();
            std::string hierPath = instance.getHierarchicalPath();

            if (visited.count(hierPath))
                continue;
            visited.insert(hierPath);

            // Check if definition matches target
            if (instance.getDefinition().name == targetName) {
                InstanceInfo instInfo;
                instInfo.instanceName = std::string(instance.name);
                instInfo.fullPath = hierPath;
                instInfo.definitionName = targetName;

                for (auto conn : instance.getPortConnections()) {
                    ConnectionInfo connInfo;
                    connInfo.portName = std::string(conn->port.name);

                    const Expression* expr = conn->getExpression();
                    if (expr) {
                        const Type& type = *expr->type;
                        connInfo.signalType = type.toString();
                        connInfo.width = std::to_string(type.getBitWidth());
                        connInfo.isConnected = true;
                    }
                    else {
                        connInfo.signalType = "Unknown";
                        connInfo.width = "0"; // Or unknown
                        connInfo.isConnected = false;
                    }
                    instInfo.connections.push_back(connInfo);
                }
                result.instances.push_back(instInfo);
            }

            // Recurse
            collectInstantiationsInAST(instance.body, targetName, result, visited);
        }
        else if (member.kind == SymbolKind::UninstantiatedDef) {
            const auto& uninst = member.as<UninstantiatedDefSymbol>();
            std::string hierPath = uninst.getHierarchicalPath();

            if (visited.count(hierPath))
                continue;
            visited.insert(hierPath);

            if (uninst.definitionName == targetName) {
                InstanceInfo instInfo;
                instInfo.instanceName = std::string(uninst.name);
                instInfo.fullPath = hierPath;
                instInfo.definitionName = targetName;

                auto portNames = uninst.getPortNames();
                auto portExprs = uninst.getPortConnections();

                for (size_t i = 0; i < portExprs.size(); i++) {
                    ConnectionInfo connInfo;
                    if (i < portNames.size() && !portNames[i].empty()) {
                        connInfo.portName = std::string(portNames[i]);
                    }
                    else {
                        connInfo.portName = "[Positional #" + std::to_string(i) + "]";
                    }

                    if (portExprs[i] && portExprs[i]->kind == AssertionExprKind::Simple) {
                        const auto& simpleExpr = portExprs[i]->as<SimpleAssertionExpr>();
                        const Expression& expr = simpleExpr.expr;
                        const Type& type = *expr.type;

                        connInfo.signalType = type.toString();
                        connInfo.width = inferWidth(expr, scope);
                        connInfo.isConnected = true;
                    }
                    else if (portExprs[i]) {
                        // Some other connection type, considered connected but maybe complex
                        connInfo.signalType = "Complex/Unresolved"; // Simplified for now
                        connInfo.width = "0";
                        connInfo.isConnected = true;
                    }
                    else {
                        connInfo.signalType = "Unconnected";
                        connInfo.width = "0";
                        connInfo.isConnected = false;
                    }
                    instInfo.connections.push_back(connInfo);
                }
                result.instances.push_back(instInfo);
            }
        }
        else if (member.isScope()) {
            collectInstantiationsInAST(member.as<Scope>(), targetName, result, visited);
        }
    }
}

// ==========================================
// Print Helper
// ==========================================
void printTextOutput(const InspectorResult& result) {
    if (result.definition) {
        const auto& def = *result.definition;
        std::cout << "[Result] Found Definition for '" << def.name << "'" << '\n';
        std::cout << "--------------------------------------------" << '\n';
        std::cout << "Source: Full Module Definition (AST)" << '\n';
        for (const auto& port : def.ports) {
            std::cout << "  Port: " << port.name << " | Dir: " << port.direction
                      << " | Type: " << port.type << '\n';
        }
        std::cout << "--------------------------------------------" << '\n';
    }

    if (!result.instances.empty()) {
        for (const auto& inst : result.instances) {
            std::cout << "[Result] Found Instantiation (AST) for '" << inst.definitionName << "'"
                      << '\n';
            std::cout << "  Instance Name: " << inst.instanceName << '\n';
            std::cout << "  Full Path: " << inst.fullPath << '\n';
            std::cout << "--------------------------------------------" << '\n';
            std::cout << "Source: AST Connection Analysis" << '\n';

            for (const auto& conn : inst.connections) {
                std::cout << "  Port: " << conn.portName;
                if (conn.isConnected) {
                    std::cout << " | Connected Signal Type: " << conn.signalType
                              << " | Width: " << conn.width;
                }
                else {
                    std::cout << " | Unconnected";
                }
                std::cout << '\n';
            }
            std::cout << "--------------------------------------------" << '\n';
        }
    }
    else if (!result.definition) {
        // Only print if neither was found? The original code printed "AST search yielded no
        // results." if foundInstances was false. But foundInAST (definition) returned earlier. We
        // will handle "Not found" in main.
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <verilog_file> <module_name> [--json <output_file>]"
                  << '\n';
        return 1;
    }

    std::string verilogFile = argv[1];
    std::string targetModuleName = argv[2];
    std::string jsonOutputFile;
    bool jsonOutput = false;

    // Simple arg parsing
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--json" && i + 1 < argc) {
            jsonOutput = true;
            jsonOutputFile = argv[++i];
        }
    }

    SourceManager sourceManager;

    auto treeResult = SyntaxTree::fromFile(verilogFile, sourceManager);
    if (!treeResult) {
        std::cerr << "Error loading file." << '\n';
        return 1;
    }
    const std::shared_ptr<SyntaxTree>& tree = *treeResult;

    CompilationOptions options;
    Compilation compilation(options);
    compilation.addSyntaxTree(tree);

    InspectorResult result;

    // Collect Definition
    collectModuleInAST(compilation, targetModuleName, result);

    // Collect Instantiations
    std::set<std::string> visited;
    collectInstantiationsInAST(compilation.getRoot(), targetModuleName, result, visited);

    bool foundAny = result.definition.has_value() || !result.instances.empty();

    if (jsonOutput) {
        json j = result;
        std::ofstream o(jsonOutputFile);
        o << j.dump(4) << std::endl;
        // Also print text output? Usually separate.
    }
    else {
        if (!foundAny) {
            std::cout << "AST search yielded no results." << '\n';
            return 1;
        }
        printTextOutput(result);
    }

    // Return 0 if found anything, or if we successfully checked even if empty?
    // Original code: if definition found -> return 0. If instances found -> return 0. Else 1.
    return foundAny ? 0 : 1;
}