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
// Find Module Instantiations (Blackbox)
// ==========================================

class BlackboxVisitor : public SyntaxVisitor<BlackboxVisitor> {
public:
    std::string targetModuleName;
    bool found = false;

    BlackboxVisitor(std::string name) : targetModuleName(std::move(name)) {}

    void handle(const HierarchyInstantiationSyntax& node) {
        if (node.type.valueText() == targetModuleName) {

            if (found)
                return;
            found = true;

            std::cout << "[Result] Found Instantiation of Blackbox '" << targetModuleName << "'"
                      << '\n';
            std::cout << "--------------------------------------------" << '\n';
            std::cout << "Source: Instantiation Syntax (Inferred from usage)" << '\n';
            std::cout << "Note: Direction and Type are unknown for blackboxes." << '\n';

            for (auto instance : node.instances) {
                for (auto connection : instance->connections) {
                    if (connection->kind == SyntaxKind::NamedPortConnection) {
                        auto& namedConn = connection->as<NamedPortConnectionSyntax>();

                        std::cout << "  Port: " << namedConn.name.valueText();
                        std::cout << " (Inferred from ." << namedConn.name.valueText() << ")"
                                  << '\n';
                    }
                    else if (connection->kind == SyntaxKind::OrderedPortConnection) {
                        std::cout << "  Port: [Unknown Name] (Positional connection detected)"
                                  << '\n';
                    }
                }
                break;
            }
            std::cout << "--------------------------------------------" << '\n';
        }
    }
};

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
    options.topModules.emplace(targetModuleName);

    Compilation compilation(options);
    compilation.addSyntaxTree(tree);

    bool foundInAST = findModuleInAST(compilation, targetModuleName);

    if (foundInAST) {
        return 0;
    }

    // Try Blackbox lookup
    std::cout << "Module definition not found. Searching for instantiations (Blackbox mode)..."
              << '\n';

    BlackboxVisitor visitor(targetModuleName);
    tree->root().visit(visitor);

    if (!visitor.found) {
        std::cerr << "Error: Module '" << targetModuleName
                  << "' not found (neither defined nor instantiated)." << '\n';
        return 1;
    }

    return 0;
}