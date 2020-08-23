#include <stdio.h>
#include <math.h>

double foo(double a) {
  return (a*sqrt(a));
}

double sum(double (*fn)(double)) {
  int i;
  double b = 2.3;

//  for (i = 0; i < 100; ++i) {
    b = fn(b);
//    b = foo(b);
 // }

  return b;
}

int main(int argc, char *argv[]) {
  double x = sum(foo);
  printf("x:%e", x);
}
