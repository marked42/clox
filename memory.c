#include <stdlib.h>
#include "compiler.h"
#include "memory.h"
#include "table.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif // DEBUG_LOG_GC

#define GC_HEAP_GROW_FACTOR 2


void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    vm.bytesAllocated += newSize - oldSize;
    if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
        collectGarbage();
#endif // DEBUG_STRESS_GC

        if (vm.bytesAllocated > vm.nextGC) {
            collectGarbage();
        }
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
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif // DEBUG

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
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            freeTable(&klass->methods);
            FREE(OBJ_CLASS, object);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            freeTable(&instance->fields);
            FREE(OBJ_INSTANCE, instance);
            break;
        }
        case OBJ_BOUND_METHOD:
            FREE(ObjBoundMethod, object);
            break;
    }
}

void freeObjects() {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }

    // set to pointer to NULL after reclamation
    free(vm.grayStack);
    vm.grayStack = NULL;
}

void markObject(Obj* object) {
    if (object == NULL) {
        return;
    }
    // skip marked object
    if (object->isMarked) {
        return;
    }

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif // DEBUG_LOG_GC

    object->isMarked = true;

    // record gray object
    if (vm.grayCount + 1 > vm.grayCapacity) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        vm.grayStack = (Obj**) realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
    }

    // exit if failed to grow gray stack;
    if (vm.grayStack == NULL) {
        exit(1);
    }

    vm.grayStack[vm.grayCount++] = object;
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

    markObject((Obj*)vm.initString);

    markCompilerRoots();
}

static void markArray(ValueArray* array) {
    for (int i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

static void blackenObject(Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif // DEBUG_LOG_GC

    switch (object->type)
    {
    case OBJ_STRING:
    case OBJ_NATIVE:
        break;
    case OBJ_UPVALUE:
        markValue(((ObjUpvalue*)object)->closed);
        break;
    case OBJ_FUNCTION:
    {
        ObjFunction* function = (ObjFunction*)object;
        markObject((Obj*)function->name);
        markArray(&function->chunk.constants);
        break;
    }
    case OBJ_CLOSURE: {
        ObjClosure* closure = (ObjClosure*) object;
        markObject((Obj*)closure->function);
        for (int i = 0; i < closure->upvalueCount; i++) {
            markObject((Obj*)closure->upvalues[i]);
        }
        break;
    }
    case OBJ_CLASS: {
        ObjClass* klass = (ObjClass*) object;
        markObject((Obj*)klass->name);
        markTable(&klass->methods);
        break;
    }
    case OBJ_INSTANCE: {
        ObjInstance* instance = (ObjInstance*)object;
        markObject((Obj*)instance->klass);
        markTable(&instance->fields);
        break;
    }

    case OBJ_BOUND_METHOD: {
        ObjBoundMethod* bound = (ObjBoundMethod*)object;
        markValue(bound->receiver);
        markObject((Obj*)bound->method);
        break;
    }

    default:
        break;
    }
}

static void traceReferences() {
    while (vm.grayCount > 0) {
        Obj* object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

static void sweep() {
    Obj* previous = NULL;
    Obj* object = vm.objects;

    // free and delete unmarked object from linked list object
    while (object != NULL) {
        if (object->isMarked) {
            // flip isMarked for next round of collection
            object->isMarked = false;

            // skip marked object
            previous = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;

            if (previous != NULL) {
                previous->next = object->next;
            } else {
                vm.objects = object->next;
            }

            freeObject(unreached);
        }
    }
}

void collectGarbage() {
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    size_t before = vm.bytesAllocated;
#endif // DEBUG_LOG_GC

    markRoots();
    traceReferences();
    tableRemoveWhile(&vm.strings);
    sweep();

    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated,
         vm.nextGC);
#endif // DEBUG_LOG_GC

}
