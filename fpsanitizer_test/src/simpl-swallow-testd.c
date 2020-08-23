#include <stdio.h>

double foo(double x){
  return ((x * 5) + 1) - (x * 5);
}

double bar(double x, double y){
  return ((x * 5) + (1 + y)) - ((x * 5) + y);
}

int main(int argc, char** argv){
  volatile double x, y, z, t;
  x = 1.0;
  y = 1.0e16;
  volatile double a, b;
  b = bar(x, 9);
  printf("b = %e\n", b);
  return 0;
}
