#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "scanner.h"
#include "compiler.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

/**
 * set local variable depth to -1 means it's uninitialized yet
 */
static const int LOCAL_VARIABLE_UNINITIALIZED = -1;

/**
 * use a negative value to represent non exist local variable index
 */
static const int LOCAL_VARIABLE_NOT_FOUND = -1;

typedef struct {
	Token previous;
	Token current;
	bool hadError;
	bool panicMode;
} Parser;

typedef void (*ParseFn)(bool canAssign);

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,  // =
  PREC_OR,          // or
  PREC_AND,         // and
  PREC_EQUALITY,    // == !=
  PREC_COMPARISON,  // < > <= >=
  PREC_TERM,        // + -
  PREC_FACTOR,      // * /
  PREC_UNARY,       // ! -
  PREC_CALL,        // . ()
  PREC_PRIMARY
} Precedence;

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int depth;
} Local;

typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler {
    struct Compiler* enclosing;

    ObjFunction *function;
    FunctionType type;

    Local locals[UINT8_COUNT];
    int localCount;
    int scopeDepth;
} Compiler;

Parser parser;
Compiler *current = NULL;
Chunk* compilingChunk;

Chunk* currentChunk() {
	return &current->function->chunk;
}

static void errorAt(Token* token, const char* message) {
	if (parser.panicMode) { return; }
	parser.panicMode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing.
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char* message) {
  errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
  errorAt(&parser.current, message);
}

static void advance() {
	parser.previous = parser.current;

	for (;;) {
		parser.current = scanToken();
		if (parser.current.type != TOKEN_ERROR) {
			break;
		}

		errorAtCurrent(parser.current.start);
	}
}

static void consume(TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) { return false; }
    advance();

    return true;
}

static void emitByte(uint8_t byte) {
	writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
	emitByte(byte1);
	emitByte(byte2);
}

static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);

    int offset = currentChunk()->count + 2 - loopStart;
    if (offset > UINT16_MAX) { error("Loop body too large."); }

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

void emitReturn() {
    emitByte(OP_NIL);
    emitByte(OP_RETURN);
}

ObjFunction *endCompiler() {
    // generate implicit return instruction if last statement is not return statement
    if (current->function->chunk.code[current->function->chunk.count - 1] != OP_RETURN) {
        emitReturn();
    }

    ObjFunction *function = current->function;

#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError)  {
        disassembleChunk(currentChunk(), function->name != NULL ? function->name->chars : "<script>");
    }
#endif

    current = current->enclosing;
    return function;
}

/**
 * condition
 * OP_JUMP_IF_FALSE     op
 * byte1                high byte of jump offset
 * byte2                low byte of jump offset
 *                      count after emitted
 *
 * @param jumpInstruction
 * @return byte1 start position of jump offset address to be patched
 */
int emitJump(uint8_t jumpInstruction) {
    emitByte(jumpInstruction);
    // value doesn't matter since it'll be patched later, but set to max to report error if not patched
    emitByte(0xff);
    emitByte(0xff);

    // index of op code jumpInstruction
    return currentChunk()->count - 2;
}

/**
 * @param offset jump offset start address index
 */
static void patchJump(int offset) {
    int jump = currentChunk()->count - offset - 2;
    if (jump > UINT16_MAX) {
        error("Too much code to jump over");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void grouping(bool canAssign) {
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static uint8_t makeConstant(Value value) {
	int constant = addConstant(currentChunk(), value);
	if (constant > UINT8_MAX) {
		error("Too many constants in one chunk");
		return 0;
	}

	return (uint8_t)constant;
}

static void emitConstant(Value value) {
	emitBytes(OP_CONSTANT, makeConstant(value));
}

static void initCompiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing = current;

    compiler->function = NULL;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->type = type;

    // for gc
    compiler->function = newFunction();

    current = compiler;
    if (type != TYPE_SCRIPT) {
        current->function->name = copyString(parser.previous.start, parser.previous.length);
    }

    // compiler claims stack slot zero for internal use
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->name.start = "";
    local->name.length = 0;
}

static void number(bool canAssign) {
	double value = strtod(parser.previous.start, NULL);

	emitConstant(NUMBER_VAL(value));
}

static void unary(bool canAssign) {
	TokenType operatorType = parser.previous.type;

	expression();

	switch (operatorType) {
		case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        case TOKEN_BANG: emitByte(OP_NOT); break;
		default: return;
	}
}

static uint8_t argumentList() {
    uint8_t argCount = 0;

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) {
                error("Can't have more than 255 arguments");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

static void call(bool canAssign) {
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;
  ParseRule* rule = getRule(operatorType);
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
    case TOKEN_PLUS:          emitByte(OP_ADD); break;
    case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
    case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
    case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
      case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
      case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
      case TOKEN_GREATER:       emitByte(OP_GREATER); break;
      case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
      case TOKEN_LESS:          emitByte(OP_LESS); break;
      case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
    default: return; // Unreachable.
  }
}

static void literal(bool canAssign) {
    switch (parser.previous.type) {
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL : emitByte(OP_NIL); break;
        default: return;
    }
}

static void string(bool canAssign) {
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static uint8_t identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* left, Token* right) {
    // quick check for strings of different length
    if (left->length != right->length) { return false; }

    return memcmp(left->start, right->start, left->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];

        if (identifiersEqual(&local->name, name)) {
            if (local->depth == LOCAL_VARIABLE_UNINITIALIZED) {
                error("Can't read local variable in it's own initializer");
            }
            return i;
        }
    }

    return LOCAL_VARIABLE_NOT_FOUND;
}

static void namedVariable(Token* token, bool canAssign) {
    int arg = resolveLocal(current, token);
    uint8_t getOp, setOp;
    // find no local variable of name token, so it's a global variable
    if (arg == LOCAL_VARIABLE_NOT_FOUND) {
        arg = identifierConstant(token);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    // local variable
    } else {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    }
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(setOp, arg);
    } else {
        emitBytes(getOp, arg);
    }
}

static void variable(bool canAssign) {
    namedVariable(&parser.previous, canAssign);
}

static void and_(bool canAssign) {
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);

    parsePrecedence(PREC_AND);

    patchJump(endJump);
}

static void or_(bool canAssign) {
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);

    patchJump(elseJump);
    emitByte(OP_POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, call,   PREC_CALL},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
  [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
  [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     binary,   PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     binary,   PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {variable,     NULL,   PREC_NONE},
  [TOKEN_STRING]        = {string,     NULL,   PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
  [TOKEN_AND]           = {NULL,     and_,   PREC_NONE},
  [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]         = {literal,     NULL,   PREC_NONE},
  [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {literal,     NULL,   PREC_NONE},
  [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE]          = {literal,     NULL,   PREC_NONE},
  [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);
    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

static void addLocal(Token name) {
    // report an error if total local count exceeds limit
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }

    Local* variable = &current->locals[current->localCount];
    variable->name = name;
    // mark local variable as uninitialized using depth
    variable->depth = LOCAL_VARIABLE_UNINITIALIZED;

    current->localCount++;
}

static void markInitialized() {
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}


static void declareVariable() {
    // skip global variable skip
    if (current->scopeDepth == 0) {
        return;
    }

    Token* name = &parser.previous;
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        // stop when exiting closest enclosing scope
        if (local->depth != LOCAL_VARIABLE_UNINITIALIZED && local->depth < current->scopeDepth) {
            break;
        }

        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }

    addLocal(*name);
}

static uint8_t parseVariable(const char* message) {
    consume(TOKEN_IDENTIFIER, message);

    declareVariable();

    // don't generate constant string for local variable name
    bool isLocalVariable = current->scopeDepth > 0;
    if (isLocalVariable) return 0;
    return identifierConstant(&parser.previous);
}

static void defineVariable(uint8_t global) {
    // skip local variable
    if (current->scopeDepth > 0) { markInitialized(); return; }
    emitBytes(OP_DEFINE_GLOBAL, global);
}

static ParseRule* getRule(TokenType type) {
  return &rules[type];
}

static void expression() {
	parsePrecedence(PREC_ASSIGNMENT);
}

ObjFunction *compile(const char* source) {
	initScanner(source);

    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

	parser.hadError = false;
	parser.panicMode = false;

	advance();

//	expression();
//	consume(TOKEN_EOF, "Expect end of expression.");
    while (!match(TOKEN_EOF)) {
        declaration();
    }

	ObjFunction *function = endCompiler();
	return !parser.hadError ? function : NULL;
}

static void synchronize() {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) { return; }

        switch (parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;

            default:
                // do nothing
                ;
        }

        advance();
    }
}

static void varDeclaration() {
    uint32_t global = parseVariable("Expect variable name");

    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        emitByte(OP_NIL);
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    defineVariable(global);
}


static void printStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value");
    emitByte(OP_PRINT);
}

static void whileStatement() {
    int loopStart = currentChunk()->count;

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    emitLoop(loopStart);
    patchJump(exitJump);

    emitByte(OP_POP);
}

static void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value");
    emitByte(OP_POP);
}

static void beginScope() {
    current->scopeDepth++;
}

/**
 * pop off local variable values of current scope from operands stack
 * pop off local variable record of current scope from scope stack
 */
static void endScope() {
    for (int i = current->localCount - 1; i >= 0; i--) {
        if (current->locals[i].depth < current->scopeDepth) {
            break;
        }
        current->localCount--;
        emitByte(OP_POP);
    }
    current->scopeDepth--;

//  reference author implementation if there's a problem
//    current->scopeDepth--;
//
//    while (current->localCount > 0 &&
//           current->locals[current->localCount - 1].depth >
//           current->scopeDepth) {
//        emitByte(OP_POP);
//        current->localCount--;
//    }
}

static void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block");
}

static void ifStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    if (match(TOKEN_ELSE)) {
        statement();
    }

    patchJump(elseJump);
}

static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters");
            }
            uint8_t constant = parseVariable("Expect parameter name");
            // TODO: not need to define ?
            defineVariable(constant);
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");

    block();

    ObjFunction* function = endCompiler();

    emitBytes(OP_CONSTANT, makeConstant(OBJ_VAL(function)));
}

static void funDeclaration() {
    uint8_t global = parseVariable("Expect function name.");
    markInitialized();
    function(TYPE_FUNCTION);
    defineVariable(global);
}

static void declaration() {
    if (match(TOKEN_VAR)) {
        varDeclaration();
    } else if (match(TOKEN_FUN)) {
        funDeclaration();
    } else {
        statement();
    }

    if (parser.panicMode) {
        synchronize();
    }
}

static void returnStatement() {
    if (current->type == TYPE_SCRIPT) {
        error("Can't return from top-level code.");
    }

    if (match(TOKEN_SEMICOLON)) {
        emitReturn();
    } else {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value");
        emitByte(OP_RETURN);
    }
}

static void statement() {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else {
        expressionStatement();
    }
}