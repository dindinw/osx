// { dg-do assemble { target i?86-*-linux* } }
// Origin: "Weidmann, Nicholas" <nicholas.weidmann@swx.ch>

typedef void (function_ptr)(int);

void foo(int)
{
}

template<function_ptr ptr> void doit(int i)
{
	__asm__("pushl %0\n\t"
		  "call *%1\n\t"
		  "popl %0"
		  :
		  : "a" (i), "b" (ptr));
}

void bar()
{
	doit<foo>(123);
}
