#ifndef Keywords_hpp
#define Keywords_hpp

#define SYMBOL(TOKEN, VALUE) inline const char *TOKEN = reinterpret_cast<const char *>(u8##VALUE)

SYMBOL(AND, "and");
SYMBOL(CASE, "case");
SYMBOL(CLASS, "class");
SYMBOL(ELSE, "else");
SYMBOL(ENDCASE, "endcase");
SYMBOL(ENDCLASS, "endclass");
SYMBOL(ENDFN, "endfn");
SYMBOL(ENDIF, "endif");
SYMBOL(ENDWHILE, "endwhile");
SYMBOL(FALSE, "false");
SYMBOL(FN, "fn");
SYMBOL(IF, "if");
SYMBOL(LET, "let");
SYMBOL(NIL, "nil");
SYMBOL(NOT, "not");
SYMBOL(OR, "or");
SYMBOL(PRINT, "print");
SYMBOL(REM, "rem");
SYMBOL(REPEAT, "repeat");
SYMBOL(RETURN, "return");
SYMBOL(THIS, "this");
SYMBOL(TRUE, "true");
SYMBOL(UNTIL, "until");
SYMBOL(WHEN, "when");
SYMBOL(WHILE, "while");

SYMBOL(PLUS, "+");
SYMBOL(MINUS, "-");
SYMBOL(TIMES, "*");
SYMBOL(DIVIDE, "/");
SYMBOL(MODULO, "%");
SYMBOL(FLOOR_DIVIDE, "//");
SYMBOL(EXPONENT, "**");

SYMBOL(EQUAL, "=");
SYMBOL(NOT_EQUAL, "/=");
SYMBOL(LESS, "<");
SYMBOL(LESS_EQUAL, "<=");
SYMBOL(GREATER_EQUAL, ">=");
SYMBOL(GREATER, ">");

SYMBOL(BITWISE_AND, "&");
SYMBOL(BITWISE_OR, "|");
SYMBOL(BITWISE_XOR, "^");
SYMBOL(BITWISE_COMPL, "~");

SYMBOL(COMMA, ",");
SYMBOL(DOT, ".");
SYMBOL(SEMICOLON, ";");
SYMBOL(COLON, ":");
SYMBOL(LEFT_PAREN, "(");
SYMBOL(RIGHT_PAREN, ")");
SYMBOL(FROM, "<-");
SYMBOL(TO, "->");
SYMBOL(IMAGINARY, "j");
SYMBOL(HEX_PREFIX, "0x");
SYMBOL(OCT_PREFIX, "0");
SYMBOL(BIN_PREFIX, "0b");

#endif
