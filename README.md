# Rin

a small, low-level programming language. Inspired by hare and c, but trying
hard not to turn into either of them (no c++-style bloat, no c#-style
"modernization"). compiles down to [qbe](https://c9x.me/compile/) IR, which
qbe then turns into machine code.

this is very early. it's basically a toy right now, but it's a toy that
actually compiles and runs native binaries, which is the fun part!

(yes, i want to make it better. no i won't focus on it 24/7 but hopefully this doesn't stay a toy forever...)

## building it

```
make
```

that gives you `rinc`, the compiler. you also need `qbe` on your PATH (it's
vendored in `qbe/`, just build it or point PATH at the prebuilt binary) and
a working `cc` to link the final output.

## using it

```
./rinc yourfile.rn -conv output_binary
./output_binary
```

or if you just want to see the generated qbe IR without going all the way
to a binary:

```
./rinc yourfile.rn -S
```

and if you were wondering, -conv is just a shortened version of "convert".

## what it looks like

```
rite add(a int, b int) int
{
    ret a + b;
}

rite main() int
{
    let x int = 5;
    mut y int = 10;
    y = y + add(x, 1);

    if (y > 10)
    {
        ret 1;
    }
    ret 0;
}
```

- `rite` declares a function (yeah, "rite" not "write", it's a naming thing)
- `let` = immutable variable, `mut` = mutable variable
- `ret` returns a value
- everything else reads pretty much like c

### calling into libc

```
extern rite puts(s char*) int;
extern rite printf(fmt char*, ...) int;

rite main() int
{
    puts("hello from rin");
    printf("the answer is %d\n", 42);
    ret 0;
}
```

`extern` functions have no body, can be variadic (`...` has to be last),
and just get linked against whatever c library provides them.

### formatted output

there's a builtin `print`/`fmt` pair, kind of like a baby version of
rust's format strings:

```
print("age: %d, pi: %f, grade: %c\n", age, pi, grade);
```

- `%d` - any integer type
- `%s` - char*
- `%c` - char
- `%f` - float
- `%%` - literal percent

`print` writes to stdout. `fmt` does the same formatting but returns the
resulting string instead of printing it, so you can build strings up before
using them.

## types

### the basics

| type    | meaning                          |
|---------|-----------------------------------|
| `void`  | nothing                          |
| `bool`  | true/false, real type, not an int in disguise |
| `char`  | one byte, unsigned                |
| `int`   | 32-bit signed                    |
| `long`  | 64-bit signed                    |
| `float` | 32-bit float                     |

### sized integers

| type  | meaning         |
|-------|-----------------|
| `i8`  | 8-bit signed    |
| `i16` | 16-bit signed   |
| `u8`  | 8-bit unsigned  |
| `u16` | 16-bit unsigned |
| `u32` | 32-bit unsigned |
| `u64` | 64-bit unsigned |

these actually wrap correctly at their own width, `u8 + u8` stays a `u8`
and overflows like you'd expect on real hardware, it doesn't secretly get
promoted to a 32-bit int and hide the overflow like c would do. div/mod/
shift/comparisons all use the right signed-vs-unsigned instruction under
the hood too, so `u32` division actually behaves like unsigned division.

a bare number literal (`let x u8 = 5;`) will happily adapt to whatever
integer type you're assigning it to. mixing two *different* concrete types
in one expression without a cast doesn't have great ergonomics yet (see
"what's missing" below).

### pointers

```
let p int* = &x;
let val int = *p;
```

`&` takes an address, `*` dereferences. pointers can stack (`int**` etc).
you can pass pointers into functions to get c-style "pass by reference."

## control flow

`if` / `else` / `else if`, `while`, `for`, all basically c syntax, all
require actual `bool` conditions (no "any nonzero number is truthy" nonsense).

```
for (mut i int = 0; i < 10; i = i + 1)
{
    print("%d\n", i);
}
```

## what's NOT here yet

let's be honest about where this stands right now:

- **no arrays.** you cannot hold more than one value without declaring more
  than one variable.
- **no pointer arithmetic.** `p + 1` on a pointer is a type error.
- **no structs.** planned...
- **no casts.** so mixing types across widths in one expression is clunky,
  literals adapt automatically, but two *different* named variables of
  different integer widths don't cleanly combine without an explicit
  conversion operator, which doesn't exist yet.
- **no modules/imports** beyond the single-file compiler (the `import`
  keyword is reserved but unused).
- **no arbitrary struct/array member access**, obviously, since there's no
  arrays or structs.

so you can't do much with this right now. Wait tight though...

## roadmap (loosely)

1. ~~sized integer types~~ - done
2. structs + field access
3. arrays
4. pointer arithmetic
5. casts
6. probably arrays-of-structs and slices after that, we'll see