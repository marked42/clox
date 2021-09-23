# Clox

[Github Repo](https://github.com/munificent/craftinginterpreters)

## 问题

1. C 语言的 switch case 中不能直接声明变量，需要包裹在显式的块中？
2. static 变量声明的含义、
3. extern 关键字的作用
4. c 语言的 separate compilation 模型
5. 作者开发的其他语言
   1. 一个 Scripting Language [Wren](https://wren.io/)
   2. http://finch.stuffwithstuff.com/index.html
   3. http://magpie-lang.org/
   4. https://github.com/munificent/vigil
   5. blog http://journal.stuffwithstuff.com/
   6. 作者参考的资料
      1. http://journal.stuffwithstuff.com/2020/04/05/crafting-crafting-interpreters/
      2. https://twitter.com/munificentbob/status/901543375945388032
      3. http://journal.stuffwithstuff.com/category/language/

## 字节码格式

1. 指令占据一个字节（byte），表示具体的操作类型，一个指令后面跟若干个字节的内容作为操作数，操作数的具体含义与指令的定义相关。
1. 对于尺寸较小且大小固定的操作数，直接使用指令后紧跟的字节代表操作数值，这样的操作数成为立即数 immediate instruction
1. 常量操作数保存在常量池中，参考 Java 实现 [constant pool](https://docs.oracle.com/javase/specs/jvms/se7/html/jvms-4.html#jvms-4.4)，指令中使用常量池中的常量时，通常使用常量在常量池中的下标位置标示。
   1. 指令 OP_CONSTANT 使用一个字节代表常量下标，最多标识 256 个常量，可以定义 OP_CONSTANT_LONG 使用 3 个字节代表常量下标。
1. 源码位置信息（行号） [run-length 编码](https://en.wikipedia.org/wiki/Run-length_encoding)

1. 内存管理，reallocate 使用了标准库函数 realloc，内部使用了 malloc 和 free，试着完全自己实现内存管理，初始状态可以向操作系统申请固定大小的内存，然后完全自己管理。

## Virtual Machine

1. bytecode 的顺序是，逆波兰表达式
1. Evaluation Order 规定从左到右，或者不规定（实现决定，存在优化空间）
1. 设计栈有最大层数，超出时出现经典的 stack overflow 错误，如果 stack 设计成可以自动扩容可以避免 stack overflow，但是对应的代价是什么？
1. 设计最小指令集 OP_NEGATE 和 OP_SUBTRACT 可以只保留一个
1. 指定实现优化，OP_NEGATE 实现为出栈、取负值、入栈三个操作，也可以实现为直接将栈顶元素去负值。单个指令实现的优化。
1. Stack based VM
1. https://en.wikipedia.org/wiki/Just-in-time_compilation
1. Register Based VM
   1. Register Base VM https://craftinginterpreters.com/a-virtual-machine.html#design-note
   1. [The Implementation of Lua 5.0](https://www.lua.org/doc/jucs05.pdf)
   1. Register Allocation https://en.wikipedia.org/wiki/Register_allocation

## Scanner

1. 区分关键字和标识符，使用 Trie 树， [v8](https://github.com/v8/v8/blob/e77eebfe3b747fb315bd3baad09bec0953e53e68/src/parsing/scanner.cc#L1643)
1. string interpolation 的实现
1. C++中 '>>'符号的解析问题
1. contextual keywords 上下文相关的关键字，C#中 await 关键字在 async 函数中是关键字，其他函数中可以作为普通标识符。

## 数据类型

数据类型的表示，基础类型 number/boolean/nil，对象类型 Obj。基础类型与对象类型的区分在于基础类型的大小固定且较小，可以直接有结构体表示，方便在栈上分配。
对象类型大小不确定且无长度限制，需要在运行时分配，适合分配在堆内存中。

宏参数需要用括号括起来，防止与其他语法冲突，如果宏参数被多次使用，且被展开的宏参数有副作用的话会造成多次副作用，下面例子中的宏使用函数实现，避免这个问题。

```c
#define IS_STRING(value) isObjType(value, OBJ_STRING)

static inline bool isObjType(Value value, ObjType type) {
return IS_OBJ(value) && AS_OBJ(value)->type == type;
}
```

使用 C 语言 struct 结构体模拟类继承效果，`ObjString`的第一个成员是基类`Obj`类型，这样任何`ObjString*`类型可以被安全的转换为基类指针类型`Obj*`。
C 语言规定结构体中字段在内存中的顺序和成员声明顺序一致，允许其中因为内存对其有 padding，成员数据之间在内存上不连续，但是第一个成员之前不能有 padding。
这样结构体的指针可以安全的转换为第一个成员类型的指针。

```c
typedef struct {
    ObjType type;
} Obj;

typedef struct {
    Obj obj;
} ObjString;

```

### 字符串

ObjString 的表示形式

1. Value -> Obj -> ObjString 使用动态分配的 Obj 中嵌套动态分配的字符串数组表示，访问字符串内容需要两次指针解引用 indirection，性能有影响。
2. 使用[flexible array members](https://en.wikipedia.org/wiki/Flexible_array_member)方法去除一层指针，
3. 每个新建的 ObjString 在堆中拷贝一个原始字符串 const char\*的副本，方便后续对象回收内存时正常调用 free 操作。但是对于只读字符串类型，多个字符串对象可以
   共享同一个内存字符串，避免多个副本。

字符串 API 的设计

1. 什么是单个字符？ ASCII, Unicode，Unicode 中有单个码点（Code Point）代表一个字符，也有多个组合字符（combining character）由多个码点组成。
2. 内存中如何表示单个码点
   1. ASCII 只支持英文
   2. UTF-32 支持所有 Unicode 字符集，定长表示,O(1)访问第 i 个字符，太浪费空间
   3. UTF-16 支持所有 Unicode 字符集,变长表示，浪费空间
   4. UTF-8 支持所有 Unicode 字符集，变长表示，空间紧凑，对于一个字节的字符来说和 ASCII 一致

TODO: string interning，Java 有显式的进行 API 进行 interning。
使用哈希表对所有字符串实例 ObjString 进行 interning 的操作，这时比较字符串相等只需要比较指针即可。

#### 哈希表

Load Factor 所有可能元素的个数/分桶个数, 衡量哈希表发生键冲突的可能性。

冲突解决：

1. Separate Chaining，最坏的情况下哈希表退化称为一个单项链表，查询操作由 O(1)退化为 O(n)，内存不友好，破坏 Locality 原则。
2. Open Addressing(closed hashing)，存在键值冲突时，寻找下一个合适的 bucket，probing，寻找 bucket 的顺序叫做 probe sequence，
   1. linear probing

哈希函数

1. deterministic 同样的输入产生同样的哈希值
2. uniform 对于一组输入，产生的哈希值应该均匀分布，避免聚集或者出现某种模式，减少冲突.
3. fast

FNV-1a

```c
static uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}
```

哈希表的操作

1. 初始化
2. 添加元素
3. 查询元素，
4. 内部操作 findEntry，存在时找到元素 Entry，不存在时找到第一个 tombstone 的位置或者空元素
5. 拷贝哈希表，不能直接拷贝内存，因为哈希表中元素插入的位置跟 size 有关，需要重新将所有元素插入一遍初始化新的哈希表。
6. 删除元素，删除元素需要使用 tombstone 元素，
7. 哈希表扩容
8. 释放内存

## 全局变量

两种语句分类

1. Declaration 声明，区别在于 Declaration 引入了一个名称绑定（Binding）。
2. Statement 普通语句

变量的相关的功能问题

1. 变量声明
   ```c
   var a = 1;
   对应bytecode
   // 效果是存储到vm的全局变量中.globals中
   //OP_DEFINE_GLOBAL "a" 1
   ```
2. 变量值读取
3. 变量值设置
4. 全局变量的名称字符串被保存到常量池中，但是同一个变量名每次使用都会生成新的变量，存在重复，浪费常量池的空间，如何优化？
5. 是否支持隐式变量声明。
6. 使用名称访问哈希表形式存储的全局变量比较慢，如何优化 ？
7. 对于可以检测出来的变量未使用情况如何进行错误处理，编译时

   ```text
   fun useVar() {
     print oops;
   }

   var ooops = "too many o's!";
   ```

## 局部变量

如果规定局部变量不能在运行时动态添加或者删除，一个局部作用域中使用的变量在编译时就完全确定，这样就可以
在编译时完成对变量的分析，将局部变量的某些操作（TODO: 具体是哪些操作，从下面的分类中做明确区分）从运行时提前到编译时，提高运行时效率。

局部变量分配在栈上，

语法作用域 (Lexical Scoping) 的含义

1. 变量定义在源码位置中所在最内层作用域
2. 外层变量内对内层作用域可见
3. 内层作用域中同名变量覆盖（shadowing）外层作用域的同名变量
4. 局部变量的生命周期从与所在作用域一致

局部变量的操作

局部变量的 ID 名称 name + 作用域嵌套深度 scopeDepth

1. 声明 declaration
2. 定义（初始化） definition/initialization
3. 初始化 两种初始化语句`var a/ var a = 1`认将 a 初始化为 nil 值，
4. 读取 未声明的变量报错（Javascript 严格模式），未初始化的变量(Javascript temporal deal zone)
5. 写入，读取变量 a 的值时，该值被保存在栈顶，此时栈中存在 a 重复的值，寄存器的虚拟机做优化。
6. 变量区分为可变和不可变
7. 同时存在的局部变量的数量限制，使用一个字节表示局部变量的栈偏移量，以下标 0 为 base，所以同时能存在的局部变量最多 256 个，可以根据需要拓展。
8. 对于 `var a = a;`的处理
9. Compiler 计算局部变量的偏移量时是线性扫描的，如何优化？
10. 错误检查
    1. 局部变量个数超过上限
    2. 重新定义局部变量
    3. 使用未初始化的局部变量 scope = -1 表示未初始化，读取变量时如果 scope = -1 表示变量 a 的初始化语句中使用了 a 本身`var a = a + 1`。

Javascript 中变量的声明

Javascript 变量隐式创建设计，造成很多 BUG。

```javascript
// VM547:1 Uncaught ReferenceError: e is not defined
let a = a + 1

// ok
var b = b + 1
```

表达式产生的值留在操作数栈顶，语句执行过程中产生副作用，栈会增加，但是执行结束后栈应该恢复到原来的高度。

局部变量语法作用域 Lexical Scope

编译时将计算局部变量偏移量，因此字节码中不存在局部变量的名称，对于 debugger 来说需要其他方式
来存储这些信息。

Javascript 中这种语法不会报错。

```javascript
// Undefined variable a
// [line 1] in script
{ print a; var a = 1; }
```

### 重复声明

同一个作用域中重新声明同名变量，Javascript var 允许 let/const 不允许， REPL 环境下允许 rust 允许。

### 问题

## 流程控制

使用 OP_JUMP_IF_FALSE/OP_JUMP 指令，接受两字节跳转参数作为 ip 跳转偏移量。

1. if/else 语句，注意语句在执行完成后操作数栈应该恢复到原来的深度，使用 OP_POP 弹出操作数
2. 逻辑操作符
   1. a and b，短路逻辑，a 为真值时不对 b 进行求值操作，表达式的值就是 a 的值；a 为假值时对 b 求值，表达式的值就是 b 的值，表达式值不被转换为布尔值。使用一个 OP_JUMP_IF_FALSE 指令和 OP_POP 指令实现。
   2. a or b, 类似逻辑，可以使用一个 OP_JUMP_IF_FALSE，一个 OP_JUMP 和一个 OP_POP 实现
   3. or 使用了三个指令会比 and 执行慢一个指令，可以通过添加新执行 OP_JUMP_IF_TRUE 实现，不添加新指令如何实现？
3. while loop condition 只能是表达式？
4. for loop initializer 可以是变量声明或者表达式，不同的处理，对于栈影响不同。
5. continue 跳转到最近一层的下次循环顶部，相当于跳过剩余循环体的执行；continue 必须被循环体包围，否则是语法错误；注意 continue 跳转出，内部 scope 结束，其中局部变量应该被清理。
6. continue label 调到指定语句
7. break 结束最近一层的循环，同样注意局部变量和作用域的处理
8. switch case
9. rust 的 loop

Goto Statement Considered Harmful

break 只能中断一层循环，中断多层循环的写法比较麻烦，使用 goto 简洁很多。

```c
// See if the matrix contains a zero.
bool found = false;
for (int x = 0; x < xSize; x++) {
  for (int y = 0; y < ySize; y++) {
    for (int z = 0; z < zSize; z++) {
      if (matrix[x][y][z] == 0) {
        printf("found");
        found = true;
        break;
      }
    }
    if (found) break;
  }
  if (found) break;
}

for (int x = 0; x < xSize; x++) {
   for (int y = 0; y < ySize; y++) {
      for (int z = 0; z < zSize; z++) {
         if (matrix[x][y][z] == 0) {
            printf("found");
            goto done;
         }
      }
   }
}
done:
```

## 函数

1. 函数定义
1. 函数调用
1. 递归支持 call stack overflow/ operand stack overflow
1. inline
1. 函数参数，参数个数限制，因为实参使用一个字节表示，所以最多支持 256 个参数，形参实参个数不一致检测。
1. 函数返回值
   1. 支持递归的函数每个函数调用都需要一个返回值，不支持递归的函数一个函数在同一个时刻只需要一个返回值，Fortran
   1. 函数可能没有显式返回值，隐式返回 NIL
   1. return 语句只能使用在函数内
1. 变长参数
1. 嵌套函数定义，嵌套层数需要限制
1. first-class function
1. native function
1. [optimization challenge](https://craftinginterpreters.com/calls-and-functions.html#challenges)
1. closure
1. tail call optimization

stack semantic

> Many Lisp implementations dynamically allocate stack frames because it simplifies implementing continuations. If your language supports continuations, then function calls do not always have stack semantics.

函数调用的栈帧格式

函数调用 OP_CALL 之前栈帧将栈帧形成这种格式，被调用的函数在栈帧开始位置，后接若干个函数实参。

fn param1 param2 param3

OP_CALL 指令的作用是创建新的栈帧，call frame，栈帧开头指向 fn。

函数调用结束时 OP_RETURN 指令，作用是将函数返回值放到栈帧开头位置。
OP_CLASS 遵循同样规则，初始化结束后将新创建的类实例放到栈帧开头位置。

## Closure

查询关键词搜索相关资料 Closure Conversion & Lambda lifting

[Closure conversion: How to compile lambda](https://matt.might.net/articles/closure-conversion/)

循环的闭包处理 loop variable closure 参考 https://craftinginterpreters.com/closures.html#design-note

闭包的设计

1. https://mrale.ph/blog/2012/09/23/grokking-v8-closures-for-fun.html
1. https://blog.mozilla.org/luke/2012/10/02/optimizing-javascript-variable-access/

闭包功能要解决的问题

### 闭包的表示

闭包等于函数定义 + 捕获的变量列表。

```c
struct Closure {
   Function function;
   // 捕获变量列表
   Upvalue** upvalues;
}
```

### 捕获变量列表

#### 编译时

编译时分析函数内使用的变量分类，局部变量、捕获变量、全局变量，其中捕获变量就是建立闭包需要的变量列表。捕获变量分成两类：直接上层函数的的局部变量、间接上层函数的局部变量。

捕获变量列表分为两类，区别在于：

1. 直接上层函数的局部变量，在运行时创建闭包函数的时候，局部变量还存在于栈上。
1. 间接上层函数的局部变量，在运行时创建闭包函数的时候，可能已经从栈上被弹出，因为闭包函数可以作为返回值传递，在其他任意时刻使用。

```js
function outer() {
  let a = 1

  function middle() {
    console.log(a)

    function inner() {
      console.log(a)
    }
  }
}
```

对于捕获变量增加间接层 Upvalue 来表示，对于捕获变量的访问都通过 Upvalue 进行。

```c
struct Upvalue {
   // 是直接上层函数的局部变量
   bool isLocal;
   // 下标
   int index;
}
```

通过对于捕获变量的分析，建立一个捕获的树结构，upvalues 字段直接引用上层 locals 或者间接引用 upvalues

```text
outer
   locals
   upvalues

middle
   locals
   upvalues

inner
   locals
   upvalues
```

树结构建立的逻辑的递归函数 resoveUpvalue

1. 判断是否是局部变量，不需要捕获 preorder
1. 捕获变量
   1. 判断是否是上层函数的局部变量，直接捕获 local:true, index
   1. 递归的调用上层函数的 resolveUpvalue 来确认变量是否被间接捕获，是的话新增间接捕获变量 local: false, index,
1. 其他情况是全局变量，没有更上层的函数环境了 post-order

注意函数内同名变量可能使用多次，所以在创建新的捕获项时先要查询是否已经存在，避免重复。

#### 运行时

捕获的是值还是变量？

```c
var globalSet;
var globalGet;

fun main() {
  var a = "initial";

  fun set() { a = "updated"; }
  fun get() { print a; }

  globalSet = set;
  globalGet = get;
}

main();
globalSet();
globalGet();
```

因为捕获的是变量，所以运行时只能为一个变量创建一个 Upvalue，保存 Upvalue 指针类型，这样所有用到该 Upvalue 的地方访问的都是同一个变量。

```c
struct Upvalue {
   // 被捕获的变量的地址
   Value* location;
}

// 运行时捕获的变量列表中每一项元素都是Upvalue指针类型。
struct Closure {
   // upvalues数组本身应该在堆上动态分配，这样函数调用结束后也能继续存在
   Upvalue* upvalues[];
}
```

使用编译时分析得到的 Upvalues 树结构，在运行时创建闭包函数实例。函数是闭包的原型，每个闭包捕获的数据是独立的。捕获的时机是函数定义的时候，此时外层函数的局部变量还在栈上。

```js
for (let i = 0; i < this.upvalues.length; i++) {
  const upvalue = upvalues[i]
  if (upvalue.local) {
    // 捕获的是局部变量的地址
    upvalues[i] = capture(frame.slots + index)
  } else {
    upvalues[i] = parent.upvalues[index]
  }
}
```

下面例子中同一个变量 a 被 inner1 和 inner 重复捕获，创建两个 Upvalue 实例，由于变量捕获的不是值，保证多次变量捕获只有一个实例。

```js
fun outer {
   var a = 1;

   fun inner1() {
      print a;
   }

   fun inner2() {
      print a;
   }
}
```

使用链表将捕获变量顺序 location 排列，变量捕获时保证链表中没有重复元素，修改 capture 的实现。

```js
function capture(location) {
  // 全局字段 openValues表示捕获变量，是捕获变量的head节点
  openValues
}
```

#### 延长捕获变量生命周期

每个被捕获的变量 captured（可以提前分析出每个变量是否被捕获）在从栈上被弹出的时候需要拷贝到堆内存中，延长其生命周期。

```c
struct Upvalue {
   Value closed;
   Value* location;
}
```

延长操作分为两步

1. 拷贝变量值到 closed 字段
1. 使用 closed 字段的地址更新 location 字段，

对于捕获变量的访问永远是通过 upvalue location 字段间接访问，这样对于访问者来说，捕获变量是在栈上还是在堆上并无区别。

延长捕获变量生命周期的操作 closeValues 调用的两个时机

1. 局部作用域结束，局部作用域中的变量需要 close
1. 函数调用结束，函数参数局部变量，需要 close

### Chp6 Activation Record 激活记录

一个语言同时支持嵌套函数和函数作为返回值的功能时，局部变量生命周期需要别延长。

> It is the combination of nested functions (where inner functions may use variables defined in the outer functions) and functions returned as results (or stored into variables) that causes local variables to need lifetimes longer than their enclosing function invocations.

1. Pascal 支持嵌套函数，但不支持函数作为返回值
1. C 支持返回函数，但是不支持嵌套函数
1. Scheme/ML 都支持，这样的函数称为高阶函数（high-order function）

栈帧（Stack Frame）也叫激活记录(Activation Record)包括了局部变量、函数参数、返回地址和其他临时变量。

> The area on the stack devoted to the local variables, parameters, re- turn address, and other temporaries for a function is called the function’s activation record or stack frame.

历史原因，栈帧从高地址向低地址增长。

栈帧的布局设计跟指令集和编程语言有关，但是使用一个指令集上推荐的标准布局可以方便跨语言函数调用。

TODO：需要一个图，描述栈帧的结构

Register Allocation

寄存器需要在函数调用时保存到栈帧中的临时变量中，区分 caller 或者 callee 负责进行这个操作，对不同变量使用不同的策略，可以减少生成指令的个数。

参数传递

使用寄存器作为参数的优势，寄存器的访问速度比内存访问快得多（2 个数量级）？

1. 叶节点调用（leaf procedure）不调用其他函数，使用寄存器保存函数的话不需要创建栈帧。
1. 对于嵌套函数调用可将参数分配到不同的寄存器上，也能避免创建栈帧。
1. 局部参数 x 可能在子函数调用时不再被使用到，这样保存在寄存器上 r 的 x 参数可以直接被覆盖而不需要保存。
1. 寄存器窗口机制，每个函数调用可以分配一组新的寄存器，不需要内存访问。

返回地址

假设函数 g 调用 f，CALL 指令地址是 a，f 的返回地址通常是 a + 1；现代的架构中更可能使用寄存器保存返回地址。

## 垃圾收集 Garbage Collection

### gc 的概念

如何判断内存不再需要？usefulness -> reachability

需要一个对于 gc 算法各种类型、概念的基础介绍，不涉及具体细节。

这里需要一张对于 gc 的分类图，最好能包含各个概念，gc 算法类型以及演变

1. 保守型（conservative）gc，c/c++中内存中每个字节可能是基本数据类型也可能是指针，无法区分，这样会存在实际上没有用到的对象没有被回收。
1. 准确型 （precise）gc，所有动态分配对象都可以准确知道是否会被用到，不会用到的被回收。

衡量 gc 算法的指标

1. 吞吐率 throughput 用户程序运行的时间占程序总运行时间（用户程序+gc）的百分比，最高是 100%。
1. 延迟时间 latency gc 程序运行的最长时间

stop-the-world 进行 gc 时用户程序停止执行

增量式 gc

扫描 native c stack 中的内存指针 https://en.wikipedia.org/wiki/Boehm_garbage_collector

1. mutator 修改对象引用关系的程序，指用户程序 collector 进行内存回收的程序，二者可能是同一个线程。

### mark sweep

分成两个阶段

1. mark 从根对象出发，可以遍历到的对象就是可触达的（reachable），可触达对象做标记 marked = 1
1. sweep 需要链表保存所有动态内存对象，遍历所有对象，没有被标记的对象就是不可触达的，需要被回收，可触达的对象重新将 marked 设置为 0

几个问题

1. 有哪些根对象，操作数栈、call frame 栈、全局变量、常量池、openvalues、compiler roots
1. intern 字符串是弱引用，不应该作为 root
1. 何时触发 gc？调用系统函数申请新的内存时，记录已经申请的总内存大小，超过初始阈值时第一次触发，后续每次调整触发阈值为已分配内存大小的倍数，系数 2.0。

1. 内存碎片
1. 延迟高 增量 gc

实现中容易出出现的问题

1. 如果一个操作需要分配一个新的动态内存对象，之后的操作会触发 gc 的话，需要把新分配的内存对象保存在操作数栈上，使得它能够被标记到，否则会被回收导致对象失效无法使用。
   1. 新增全局常量
   1. 新增 intern 的字符串
   1. 字符串拼接，如果先将两个字符串从栈上弹出，然后申请新内存可能触发 gc，这时弹出的字符串不会被标记到从而被回收，然后进行拼接时访问原来的两个字符串会出错。

### mark sweep compact

### g1 gc

### incremental

### zing gc

### 资料

1. http://journal.stuffwithstuff.com/2013/12/08/babys-first-garbage-collector/

## 面向对象

基于类的面向对象表示

1. 类本身 ObjClass
1. 实例 ObjInstance
1. 类的声明 ClassDeclaration
1. 创建类实例
   1. 构造函数调用的时，栈帧上第一个槽作为 this 变量对应值，同时也是新初始化的实例对象。
   1. 有显式声明的构造函数 init，支持传入函数参数，没有 init 函数时创建新实例的构造函数调用不能接受任何参数。
   1. 构造函数返回值隐式的是实例本身，即 this 变量，构造函数中可以使用不带参数 return;表示提前结束，但是不能使用带参数的返回语句 return 1;
   1. 构造函数编译为字节码指令时将 return 语句编译为默认输出第一个局部变量（this，也就是新初始化的实例值）。
1. 实例属性读写，成员表达式解析，根据读写生成对应的字节码 OP_GET_PROPERTY/OP_SET_PROPERTY
   1. 读写都要检查 实例是否是 ObjInstance 类型
   1. 读需要在属性不存在是报运行时错误
1. 类实例是否允许动态添加删除字段（Field）读写不存在的 Field 如何处理？如何优化属性读写性能，在编译时确认所有类实例字段？
1. 访问实例属性表达式中，object 不是对象实例如何处理？
1. 定义类方法
   1. 在类结构体 ObjClass 增加 Table methods 字段代表函数体，每个函数定义有三个信息，所属类、函数名、函数本身
   1. 实例方法属性访问返回 ObjBoundMethod 对象在栈顶，记录了对应的实例对象，receiver，这样方法作为值传递到其他地方调用的时候其中 this 参数隐式的绑定到 receiver 上。
   1. 类方法编译时，this 被当做普通变量处理，好处在于 this 会像其他局部变量一样自动被捕获。this 被存放在局部变量数组的第一个下标位置中，局部变量数组初始化时刻意保留第一个下标做此用途。
1. 类方法调用，处理 this 绑定，this 是 locals 的第一个，stack 上将 frame 的第一个位置（方法值）替换为 this，因为方法被调用后要弹出，不需要保存，所以可以占用。 类方法可以传递给其他对象 BoundMethod = Closure + Instance
   1. 检测 this 使用的位置，必须在类方法中，不允许出现在函数中，但是允许出现在嵌套在方法中的函数中，此时函数中使用 this 访问到外层方法的 this，使用一个变量记录当前 class 来进行判断。
   1. 优化将 instance.method() 优化为单独的一条执行指令，而不是先创建 BoundMethod 再执行 OP_INVOKE name argCount，动态添加属性函数的情况，不能只从类静态定义中寻找方法调用。

```c
class Oops {
  init() {
    fun f() {
      print "not a method";
    }

    this.field = f;
  }
}

var oops = Oops();
oops.field();
```

1. 构造函数的处理，返回值是初始化的类实例，禁止使用 return？
1. 嵌套类？

字节码优化技巧

1. super instruction，将经常出现的连续字节指令模式合成一个指令。

左值的语法约束？
