#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"

static void repl() {
	char line[1024];
	for(;;) {
		printf("> ");
		if (!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}
		// printf("hello");
		interpret(line);
	}
}

static char* readFile(const char* path) {
	FILE* file = fopen(path, "rb");

	if (file == NULL) {
		fprintf(stderr, "Could not open file \"%s\".\n", path);
		exit(74);
	}

	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char* buffer = (char*)malloc(fileSize + 1);
	if (buffer == NULL) {
		fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
		exit(74);
	}
	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	if (bytesRead < fileSize) {
		fprintf(stderr, "Could not read file \"%s\".\n", path);
	}
	buffer[bytesRead] = '\0';

	fclose(file);
	return buffer;
}

static void runFile(const char* path) {
	char* source = readFile(path);
	InterpertResult result = interpret(source);
	free(source);

	if (result == INTERPRET_COMPILE_ERROR) exit(65);
	if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

int main(int argc, const char* argv[]) {
	initVM();

	// const char* source = "print 1;";
	// const char* source = "fun areWeHavingItYet() {"
	// 	"print \"Yes we are!\";"
	// 	"}"
	// 	"print 1 + 2;"

	// 	"print areWeHavingItYet;"
	// ;

	// const char* source =
	// 	"fun a() { b(); }"
	// 	"fun b() { c(); }"
	// 	"fun c() { c(\"too\", \"many\"); }"
	// 	"a();";

	// const char* source =
	// 	"fun sum(a, b) { return a + b; }"
	// 	"sum(1, 2);";

	// const char* source = "print clock();";

// 	const char* source =
// "var x = \"global\";"
// "fun outer() {"
// "  var x = \"outer\";"
// "  fun inner() {"
// "    print x;"
// "  }"
// "  inner();"
// "}"
// "outer();";

// 	const char* source =
// "fun outer() {"
// "  var x = \"outside\";"
// "  fun inner1() {"
// "    print x;"
// "  }"
// "  fun inner2() {"
// "    print x;"
// "  }"
// "}";

// 	const char* source =
// "fun a() {"
// "	print \"a\";"
// "}"
// "fun b() {"
// "	a();"
// "}"
// "b();";

	// const char* source = "print 1;";

	// empty class declaration
	// const char* source = "{ class Test {}  print Test; }";

	// call class as constructor function without new keyword to create new instance
	// const char* source = "class Test {} print Test();";

	// 属性读写
// 	const char* source =
// "class Toast {}"
// "var toast = Toast();"
// "print toast.jam = \"grape\";";

	// 属性读写例子2
	const char* source =
"class Pair {}"
"var pair = Pair();"
"pair.first = 1;"
"pair.second = 2;"
"print pair.first + pair.second;";

// 	const char* source =
// "fun outer() {"
// "  var a = 1;"
// "  var b = 2;"
// "  fun middle() {"
// "    var c = 3;"
// "    var d = 4;"
// "    fun inner() {"
// "      print a + c + b + d;"
// "    }"
// "  }"
// "}";

	interpret(source);

	// if (argc == 1) {
	// 	repl();
	// } else if (argc == 2) {
	// 	runFile(argv[1]);
	// } else {
	// 	fprintf(stderr, "Usage: clox [path]\n");
	// 	exit(64);
	// }

	freeVM();

	return 0;
}
