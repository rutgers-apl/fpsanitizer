#include <stdio.h>
#include <math.h>
int main() {
  volatile double v,w,x,y,z;
  v = 293827e-5;
  y = 713924e-3;
  w = 713924e-3;
  x = v * w;
  y = v - y;
  z = x * y;
  return 0;
}
