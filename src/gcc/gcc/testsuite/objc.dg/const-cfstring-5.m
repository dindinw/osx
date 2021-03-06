/* APPLE LOCAL file 4154928 */
/* Test if constant CFStrings may be passed back as ObjC strings.  */
/* Author: Ziemowit Laski  */

/* { dg-options "-fconstant-cfstrings" } */
/* { dg-do compile { target *-*-darwin* } } */

#include <objc/Object.h>

@interface Foo: Object {
  char *cString;
  unsigned int len;
}
+ (Foo *)description;
@end

@interface Bar: Object
+ (Foo *) getString: (int) which;
@end

@implementation Bar
+ (Foo *) getString: (int) which {
  return which? [Foo description]: @"Hello";
}
@end
