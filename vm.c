#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "vm.h"
#include "common.h"
#include "value.h"
#include "debug.h"
#include "compiler.h"
#include "chunk.h"
#include "memory.h"

VM vm;

void resetStack() {
	vm.stackTop = vm.stack;
    vm.frameCount = 0;
}

void initVM() {
	resetStack();
    vm.objects = NULL;
    initTable(&vm.strings);
    initTable(&vm.globals);
}

void freeVM() {
    freeObjects();
    freeTable(&vm.strings);
    freeTable(&vm.globals);
}

static void printStackTrace() {
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame *frame = &vm.frames[i];
        ObjFunction* function = frame->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }
}

static void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    printStackTrace();
    // size_t instruction = vm.ip - vm.chunk->code - 1;
    // int line = vm.chunk->lines[instruction];
    // fprintf(stderr, "[line %d] in script\n", line);

    resetStack();
}

static bool call(ObjFunction* function, int argCount) {
    if (argCount != function->arity) {
        runtimeError("Expected %d arguments but got %d", function->arity, argCount);
        return false;
    }

    if (vm.frameCount == FRAME_MAX) {
        runtimeError("Stackoverflow");
        return false;
    }

    // push new frame on top of call stack
    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->function = function;
    frame->ip = function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1;

    return true;
}

InterpertResult interpret(const char* source) {
    ObjFunction * function = compile(source);
	if (function == NULL) {
		return INTERPRET_COMPILE_ERROR;
	}

    push(OBJ_VAL(function));
    call(function, 0);
    // CallFrame* frame = &vm.frames[vm.frameCount++];

    // frame->function = function;
    // frame->ip = function->chunk.code;
    // frame->slots = vm.stack;

	InterpertResult result = run();

	return result;
}

void push(Value value) {
	*vm.stackTop = value;
	vm.stackTop++;
}

Value pop() {
	vm.stackTop--;
	return *vm.stackTop;
}

static Value peek(int offset) {
    return vm.stackTop[- 1 - offset];
}

static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee))
        {
        case OBJ_FUNCTION:
            return call(AS_FUNCTION(callee), argCount);

        default:
            break;
        }
    }
    runtimeError("Can only call functions and classes");
    return false;
}


static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
    ObjString* b = AS_STRING(pop());
    ObjString* a = AS_STRING(pop());

    int length = a->length + b->length;
    char* chars = ALLOCATE(char*, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    push(OBJ_VAL(result));
}

static InterpertResult run() {
    CallFrame *frame = &vm.frames[vm.frameCount - 1];
#define READ_BYTE() (*frame->ip++)
#define READ_CONSTANT() (frame->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() (AS_STRING(READ_CONSTANT()))
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define BINARY_OP(valueType, op) \
    do {              \
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtimeError("Operands must be number.s");         \
        return INTERPRET_RUNTIME_ERROR;\
        }\
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false)


    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        printf("--         ");
        for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        disassembleInstruction(&frame->function->chunk, (int)(frame->ip - frame->function->chunk.code));
#endif
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_RETURN: {
                Value result = pop();
                vm.frameCount--;
                if (vm.frameCount == 0) {
                    pop();
                    return INTERPRET_OK;
                }

                vm.stackTop = frame->slots;
                push(result);
                frame = &vm.frames[vm.frameCount - 1];

                break;
            }
            case OP_NEGATE:
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("operand must be a number");
                }
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;
            case OP_ADD:
            {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a + b));
                } else {
                    runtimeError("Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
            }
                break;
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
            case OP_CONSTANT:
            {
                // 变量声明必须位于括号内
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_NIL:
                push(NIL_VAL);break;
            case OP_TRUE:
                push(BOOL_VAL(true));break;
            case OP_FALSE:
                push(BOOL_VAL(false)); break;
            case OP_NOT:
                push(BOOL_VAL(isFalsey(pop())));
                break;
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
            case OP_PRINT:
                printValue(pop());
                printf("\n");
                break;
            case OP_POP:
                pop();
                break;
            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable %s", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString *name = READ_STRING();
                Value value;
                if (tableSet(&vm.globals, name, value)) {
                    tableDelete(&vm.globals, name) ;
                    runtimeError("Undefined variable %s", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                // assignment is an expression,
                frame->slots[slot] = peek(0);
//                vm.stack[slot] = peek(0);
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) {
                    frame->ip += offset;
//                    vm.ip += offset;
                }
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
//                vm.ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
//                vm.ip -= offset;
                frame->ip -= offset;
                break;
            }
            case OP_CALL: {
                int argCount = READ_BYTE();
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                // TODO: frame - ?
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
        }
    }

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
#undef READ_SHORT
}