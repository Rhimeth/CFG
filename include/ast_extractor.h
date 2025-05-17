#ifndef AST_EXTRACTOR_H
#define AST_EXTRACTOR_H

#include <clang/AST/ASTContext.h>
#include <QJsonObject>
#include <QJsonArray>
#include <string>

class ASTExtractor {
public:
    ASTExtractor();
    virtual ~ASTExtractor();

    void extractAST(clang::ASTContext& context, const std::string& outputPath);

private:
    void extractDecl(const clang::Decl* decl, QJsonObject& astJson);
};

#endif // AST_EXTRACTOR_H