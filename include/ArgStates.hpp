#ifndef ArgStates_H
#define ArgStates_H
// The plugin receives ONE global symbol as input.
// Since we only need to look at changed entities (and not all
// symbols as with AddSuffix) the overhead of doing a new
// run per name is not going to be notable a problem
//
// We want to determine what arguments are used to call each of these
// functions. Our record of this data will be on the form
//
//  {
//    "XML_ExternalEntityParserCreate": {
//        "param1": [
//          "0", "1"
//        ],
//        "param2": [
//          "getchar()", "0"
//        ]
//    }
//  }
//
//  The params which are only used with finite values as arguments can
//  be restricted during harness generation.
//
//  Note that the argument names in EUF are derived
//  from calls (not declarations) so it is integral that parameters in
//  the output from the plugin follow the call order.
//  We therefore use a vector for the arguments
//  rather than a map or list.
//
//  https://clang.llvm.org/docs/LibASTMatchersTutorial.html
//

#include "Base.hpp"

//-----------------------------------------------------------------------------
// First pass:
// In the first pass we will determine every call site to
// a changed function and what arguments the invocations use
//-----------------------------------------------------------------------------
class FirstPassMatcher : public MatchFinder::MatchCallback {
public:
  explicit FirstPassMatcher() {}
  // Defines what types of nodes we we want to match
  void run(const MatchFinder::MatchResult &) override;
  void onEndOfTranslationUnit() override {};

  std::vector<ArgState> argumentStates;
  std::string filename;
private:
  void getCallPath(DynTypedNode &parent, std::string bindName,
    std::vector<DynTypedNode> &callPath);
  void handleLiteralMatch(variants value,
    StateType matchedType, const CallExpr* call, const Expr* matchedExpr);
  std::tuple<std::string,int> getParam(const CallExpr* matchedCall,
   std::vector<DynTypedNode>& callPath,
   const char* bindName);

  SourceManager* srcMgr;
  BoundNodes::IDToNodeMap nodeMap;

  // Holds contextual information about the AST, this allows
  // us to determine e.g. the parents of a matched node
  ASTContext* ctx;
};

class FirstPassASTConsumer : public ASTConsumer {
public:
  FirstPassASTConsumer(std::string symbolName);
  void HandleTranslationUnit(ASTContext &ctx) override ;

  FirstPassMatcher matchHandler;
private:
  MatchFinder finder;
};


//-----------------------------------------------------------------------------
// Second pass:
// In the second pass we will consider all of the DECLREF arguments
// found from the previous pass and determine their state space
// before the function call occurs
//-----------------------------------------------------------------------------
class SecondPassMatcher : public MatchFinder::MatchCallback {
public:
  explicit SecondPassMatcher() {}
  void run(const MatchFinder::MatchResult &) override;
  void onEndOfTranslationUnit() override {};

  std::vector<ArgState> argumentStates;
private:
  SourceManager* srcMgr;
  BoundNodes::IDToNodeMap nodeMap;

  // Holds contextual information about the AST, this allows
  // us to determine e.g. the parents of a matched node
  ASTContext* ctx;
};

class SecondPassASTConsumer : public ASTConsumer {
public:
  SecondPassASTConsumer(std::string symbolName);
  void HandleTranslationUnit(ASTContext &ctx) override ;

  SecondPassMatcher matchHandler;

private:
  MatchFinder finder;
};

//-----------------------------------------------------------------------------
// ASTConsumer driver for each pass
//  https://stackoverflow.com/a/46738273/9033629
//-----------------------------------------------------------------------------
class ArgStatesASTConsumer : public ASTConsumer {
public:
  ArgStatesASTConsumer(std::string symbolName) ;
  ~ArgStatesASTConsumer();
  void HandleTranslationUnit(ASTContext &ctx) override;

private:
  void dumpArgStates();
  std::string getOutputPath();
  std::string symbolName;
  std::string filename;
  std::vector<ArgState> argumentStates;
};

#endif
