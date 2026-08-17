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

## arrays

fixed-size arrays, C-style declaration:

```
let nums int[5] = [1, 2, 3, 4, 5];
print("first: %d, len: %d\n", nums[0], len(nums));

mut squares int[4] = [0, 0, 0, 0];
mut i int = 0;
while (i < 4)
{
    squares[i] = i * i;
    i = i + 1;
}
```

- indexing is raw & C-style. There's no bounds checking. `arr[i]` on a bad `i` is
  undefined behavior, like C. you're trusted with it.
- `len(arr)` gets you the element count. for a fixed array this is a
  compile-time constant (free), for a slice parameter (see below) it's
  loaded at runtime.
- you cannot assign to a whole array (`arr = other;` is a compile error);
  assign element by element instead.
- no arrays of arrays yet.

### passing arrays to functions

a function parameter can be declared as an open/slice type with `T[]`
(no size). when you pass a fixed `T[N]` array as that argument, Rin
automatically passes it as a fat pointer: the base address plus the
length so the callee knows how big the array actually is without you
threading a separate length parameter everywhere:

```
rite sum(xs int[]) long
{
    mut total long = 0;
    mut i int = 0;
    while (i < len(xs))
    {
        total = total + xs[i];
        i = i + 1;
    }
    ret total;
}

rite main() int
{
    let nums int[5] = [1, 2, 3, 4, 5];
    print("%d\n", sum(nums)); /* 15 */
    ret 0;
}
```

since it's a pointer under the hood, a slice parameter can also be
used to mutate the caller's array (pass by reference, same spirit as
passing a plain pointer):

```
rite bump(xs int[]) void
{
    mut i int = 0;
    while (i < len(xs))
    {
        xs[i] = xs[i] + 1;
        i = i + 1;
    }
}
```

current limitations on this: `T[]` can only be used as a parameter type
(you can't declare a local variable of slice type), and a slice parameter
can't currently be forwarded on as an argument to another function
expecting a slice - only a named fixed-size array can be passed into a
slice parameter directly. both are reasonable things to lift later.

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

- **arrays are fixed-size and one level deep.** no arrays of arrays, and
  a slice (`T[]`) can only be a parameter type, not a local variable, and
  can't be forwarded from one slice parameter into another function.
- **no pointer arithmetic beyond indexing.** `p + 1` on a pointer is a type error, though `p[1]` now works (indexing accepts pointers as well as arrays).
- **no structs.** planned...
- **no casts.** so mixing types across widths in one expression is clunky,
  literals adapt automatically, but two *different* named variables of
  different integer widths don't cleanly combine without an explicit
  conversion operator, which doesn't exist yet.
- **no modules/imports** beyond the single-file compiler (the `import`
  keyword is reserved but unused).

## roadmap (loosely)

1. ~~sized integer types~~ ✅ done
2. ~~arrays~~ ✅ partly done (fixed-size, single-level, slice params)
3. structs + field access
4. pointer arithmetic
5. casts
6. probably arrays-of-structs after that, we'll see