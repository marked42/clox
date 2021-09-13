//
// Created by 张鹏辉 on 2021/9/5.
//

#ifndef CLOX1_OBJECT_H
#define CLOX1_OBJECT_H

#include "common.h"
#include "value.h"
#include "chunk.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_STRING(value) isObjType(value, OBJ_STRING)

#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
} ObjType;

struct Obj {
    ObjType type;
    struct Obj* next;
};

// TODO: 去掉ObjString编译出错 typedef struct {
typedef struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;
} ObjString;

typedef struct {
    Obj obj;
    int arity;
    Chunk chunk;
    int upvalueCount;
    ObjString* name;
} ObjFunction;

typedef struct ObjUpvalue {
  Obj obj;
  Value* location;
  struct ObjUpvalue* next;
  Value closed;
} ObjUpvalue;

typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalueCount;
} ObjClosure;

typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct {
    Obj obj;
    NativeFn function;
} ObjNative;

ObjUpvalue* newUpvalue(Value* slot);

ObjClosure* newClosure(ObjFunction* function);

ObjFunction* newFunction();

ObjNative* newNative(NativeFn function);

ObjString* takeString(const char* chars, int length);

ObjString* copyString(const char* chars, int length);

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

void printObject(Value value);

#endif //CLOX1_OBJECT_H
