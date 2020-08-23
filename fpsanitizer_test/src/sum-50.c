#include <stdio.h>

int main() {
  volatile double x = 0;
  while(x < 10.0){
    x += 0.2;
  }
  printf("%.20g\n", x);
}
