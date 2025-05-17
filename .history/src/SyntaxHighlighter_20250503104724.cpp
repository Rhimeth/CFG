#include "SyntaxHighlighter.h"

SyntaxHighlighter::SyntaxHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent)
{
    // C++ syntax rules
    HighlightingRule rule;
    
    // Keywords
    QStringList keywords = {
        "char", "class", "const", "double", "enum", "explicit",
        "friend", "inline", "int", "long", "namespace", "operator",
        "private", "protected", "public", "short", "signals", "signed",
        "slots", "static", "struct", "template", "typedef", "typename",
        "union", "unsigned", "virtual", "void", "volatile"
    };
    
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(Qt::darkBlue);
    keywordFormat.setFontWeight(QFont::Bold);
    
    foreach (const QString& keyword, keywords) {
        rule.pattern = QRegExp("\\b" + keyword + "\\b");
        rule.format = keywordFormat;
        highlightingRules.append(rule);
    }
    
    // More rules can be added for numbers, strings, comments etc.
}

void SyntaxHighlighter::highlightBlock(const QString& text)
{
    foreach (const HighlightingRule& rule, highlightingRules) {
        QRegExp expression(rule.pattern);
        int index = expression.indexIn(text);
        while (index >= 0) {
            int length = expression.matchedLength();
            setFormat(index, length, rule.format);
            index = expression.indexIn(text, index + length);
        }
    }
}