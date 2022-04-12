//==============================================================================
// DESCRIPTION: ArgStates
//
// USAGE:
//    1. As a loadable Clang plugin:
//      clang -cc1 -load <BUILD_DIR>/lib/libArgStates.dylib  -plugin  '\'
//      ArgStates -plugin-arg-ArgStates -class-name '\'
//      -plugin-arg-ArgStates Base  -plugin-arg-ArgStates -old-name '\'
//      -plugin-arg-ArgStates run  -plugin-arg-ArgStates -new-name '\'
//      -plugin-arg-ArgStates walk test/ArgStates_Class.cpp
//    2. As a standalone tool:
//       <BUILD_DIR>/bin/ct-code-refactor --class-name=Base --new-name=walk '\'
//        --old-name=run test/ArgStates_Class.cpp
//
//==============================================================================
#include "ArgStates.hpp"

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Tooling/Refactoring/Rename/RenamingAction.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <fstream>
#include <unordered_set>

using namespace clang;
using namespace ast_matchers;

//-----------------------------------------------------------------------------
// ArgStatesMatcher - implementation
// Add the suffix to matched items
//-----------------------------------------------------------------------------

/// Recursivly enumerate the children of the given statement
/// and print the bottom level nodes
void ArgStatesMatcher::getChildren(const Stmt* stmt, ASTContext* ctx) {
      bool hasChild = false;
      for (auto child : stmt->children()) {
        hasChild = true;
        this->getChildren(child, ctx);
      }

      if (!hasChild){
        // If the statement is a reference to a decleration
        // fetch the declaretion
        // stmt->getStmtClassName()
        switch(stmt->getStmtClass()){
          case Stmt::NullStmtClass:
            llvm::errs() << "Null statement\n";
            break;
          case Stmt::IntegerLiteralClass:
            llvm::errs() << "Int literal statement\n";
            break;
          case Stmt::DeclRefExprClass:
            llvm::errs() << "Ref statement " << "\n";
            break;
          default:
            stmt->dumpColor();
        }
      }
}

/// The idea:
/// Determine what types of arguments are passed to the function
/// For literal and NULL arguments, we add their value to the state space
/// For declrefs, we go up in the AST until we reach the enclosing function
/// and record all assignments to the declref
/// For other types, we set nondet for now
void ArgStatesMatcher::run(const MatchFinder::MatchResult &result) {
    // Holds information on the actual sourc code
    const auto srcMgr = result.SourceManager;

    // Holds contxtual information about the AST, this allows
    // us to determine e.g. the parents of a matched node
    const auto ctx = result.Context;

    const auto *call       = result.Nodes.getNodeAs<CallExpr>("CALL");
    const auto *func       = result.Nodes.getNodeAs<FunctionDecl>("FNC");
    
    // Only matching onl declref will produce issues e.g. when we have nodes
    // on the form 'dtd->pool', declref will only match dtd
    // we could techincally miss stuff if pool is indirectly changed
    // through a reference of dtd or if there is an aliased ptr.
    const auto *declRef    = result.Nodes.getNodeAs<DeclRefExpr>("REF");

    const auto *intLiteral = result.Nodes.getNodeAs<IntegerLiteral>("INT");
    const auto *strLiteral = result.Nodes.getNodeAs<StringLiteral>("STR");
    const auto *chrLiteral = result.Nodes.getNodeAs<CharacterLiteral>("CHR");

    if (declRef) {
      const auto location = srcMgr->getFileLoc(declRef->getEndLoc());

      llvm::errs() << "REF> " << location.printToString(*srcMgr)
        << " " << declRef->getDecl()->getName()
        << "\n";
      //declRef->dumpColor();
    }
    if (intLiteral) {
      const auto location = srcMgr->getFileLoc(intLiteral->getLocation());

      llvm::errs() << "INT> " << location.printToString(*srcMgr)
        << " " << intLiteral->getValue()
        << "\n";
    }
    if (strLiteral) {
      llvm::errs() << "STR> "
        << " " << strLiteral->getString()
        << "\n";
    }
    if (chrLiteral) {
      const auto location = srcMgr->getFileLoc(chrLiteral->getLocation());

      llvm::errs() << "CHR> " << location.printToString(*srcMgr)
        << " " << chrLiteral->getValue()
        << "\n";
    }
}

void ArgStatesMatcher::onEndOfTranslationUnit() {
  // Output to stdout
  //ArgStatesRewriter
  //    .getEditBuffer(ArgStatesRewriter.getSourceMgr().getMainFileID())
  //    .write(llvm::outs());
}

//-----------------------------------------------------------------------------
// ArgStatesASTConsumer- implementation
//  https://clang.llvm.org/docs/LibASTMatchersTutorial.html
// Specifies the node patterns that we want to analyze further in ::run()
//-----------------------------------------------------------------------------

ArgStatesASTConsumer::ArgStatesASTConsumer(
    Rewriter &R, std::vector<std::string> Names
    ) : ArgStatesHandler(R), Names(Names) {
  // We want to match agianst all variable refernces which are later passed
  // to one of the changed functions in the Names array
  //
  // As a starting point, we want to match the FunctionDecl nodes of the enclosing
  // functions for any call to a changed function. From this node we can then
  // continue downwards until we reach the actual call of the changed function,
  // while recording all declared variables and saving the state of those which end up being used
  //
  // If we match the call experssions directly we would need to backtrack in the AST to find information
  // on what each variable holds

  #if DEBUG_AST
  llvm::errs() << "\033[33m!>\033[0m Processing " << Names[0] << "\n";
  #endif

  // To access the parameters to a call we need to match the actual call experssion
  // The first child of the call expression is a declRefExpr to the function being invoked
  // Match references to the changed function
  const auto isArgumentOfCall = hasAncestor(callExpr(
    callee(functionDecl(hasName(Names[0])).bind("FNC"))
  ).bind("CALL"));


  const auto declRefMatcher = declRefExpr(to(
    declaratorDecl()), isArgumentOfCall
  ).bind("REF");
  const auto intMatcher = integerLiteral(isArgumentOfCall).bind("INT");
  const auto stringMatcher = stringLiteral(isArgumentOfCall).bind("STR");
  const auto charMatcher = characterLiteral(isArgumentOfCall).bind("CHR");

  this->Finder.addMatcher(declRefMatcher, &(this->ArgStatesHandler));
  this->Finder.addMatcher(intMatcher,     &(this->ArgStatesHandler));
  this->Finder.addMatcher(stringMatcher,  &(this->ArgStatesHandler));
  this->Finder.addMatcher(charMatcher,    &(this->ArgStatesHandler));
}

//-----------------------------------------------------------------------------
// FrontendAction
//-----------------------------------------------------------------------------
class ArgStatesAddPluginAction : public PluginASTAction {
public:
  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) override {
    DiagnosticsEngine &diagnostics = CI.getDiagnostics();

    unsigned namesDiagID = diagnostics.getCustomDiagID(
      DiagnosticsEngine::Error, "missing -names-file"
    );

    for (size_t i = 0, size = args.size(); i != size; ++i) {

      if (args[i] == "-names-file") {
         if (parseArg(diagnostics, namesDiagID, size, args, i)){
             auto NamesFile = args[++i];
             this->readNamesFromFile(NamesFile);
         } else {
             return false;
         }
      }
      if (!args.empty() && args[0] == "help") {
        llvm::errs() << "No help available";
      }
    }

    return true;
  }

  // Returns our ASTConsumer per translation unit.
  // This is the entrypoint
  // https://clang.llvm.org/docs/RAVFrontendAction.html
  std::unique_ptr<ASTConsumer> CreateASTConsumer(
      CompilerInstance &CI, StringRef file
  ) override {

    RewriterForArgStates.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());

    return std::make_unique<ArgStatesASTConsumer>(
        RewriterForArgStates, this->Names
    );
  }

private:
  void readNamesFromFile(std::string filename) {
    std::ifstream file(filename);

    if (file.is_open()) {
      std::string line;

      while (std::getline(file,line)) {
        this->Names.push_back(line);
      }
      file.close();
    }
  }

  bool parseArg(DiagnosticsEngine &diagnostics, unsigned diagID, int size,
      const std::vector<std::string> &args, int i) {

      if (i + 1 >= size) {
        diagnostics.Report(diagID);
        return false;
      }
      if (args[i+1].empty()) {
        diagnostics.Report(diagID);
        return false;
      }
      return true;
  }

  Rewriter RewriterForArgStates;
  std::vector<std::string> Names;
};

//-----------------------------------------------------------------------------
// Registration
//-----------------------------------------------------------------------------
static FrontendPluginRegistry::Add<ArgStatesAddPluginAction>
    X(/*NamesFile=*/"ArgStates",
      /*Desc=*/"Enumerate the possible states for arguments to calls of the functions given in the -names-file argument.");
