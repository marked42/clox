#include <stdlib.h>
#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif // DEBUG_LOG_GC

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
        collectGarbage();
#endif // DEBUG_STRESS_GC
    }
	if (newSize == 0) {
		free(pointer);
		return NULL;
	}

	void* result = realloc(pointer, newSize);
	// memory allocation failed
	if (result == NULL) exit(1);
	return result;
}

static void freeObject(Obj* object) {
    switch (object->type) {
        case OBJ_STRING:
        {
            ObjString *string = (ObjString*)object;
            FREE_ARRAY(char , string->chars, string->length + 1);
            FREE(ObjString, object);
            break;
        }
        case OBJ_FUNCTION:
        {
            ObjFunction *function = (ObjFunction*) object;
            freeChunk(&function->chunk);
            FREE(ObjFunction, object);
            break;
        }
        case OBJ_NATIVE: {
            FREE(OBJ_NATIVE, object);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
            FREE(OBJ_CLOSURE, object);
            break;
        }
        case OBJ_UPVALUE: {

            FREE(ObjUpvalue, object);
            break;
        }
    }

#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif // DEBUG
}

void freeObjects() {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
}

void markObject(Obj* obj) {
    if (obj == NULL) {
        return;
    }

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*)obj);
    printValue(OBJ_VAL(obj));
    printf("\n");
#endif // DEBUG_LOG_GC

    obj->isMarked = true;
}

void markValue(Value value) {
    if (IS_OBJ(value)) {
        markObject(AS_OBJ(value));
    }
}

static void markRoots() {
    // mark stack
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        markValue(*slot);
    }

    // mark call stack
    for (int i = 0; i < vm.frameCount; i++) {
        markObject((Obj*)vm.frames[i].closure);
    }

    // mark open values
    for (ObjUpvalue* upvalue = vm.openValues; upvalue != NULL; upvalue = upvalue->next) {
        markObject((Obj*)upvalue);
    }

    markTable(&vm.globals);

    markCompilerRoots();
}

void collectGarbage() {
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
#endif // DEBUG_LOG_GC

    markRoots();

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
#endif // DEBUG_LOG_GC
}
