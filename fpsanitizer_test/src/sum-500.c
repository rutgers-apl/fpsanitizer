#include <stdio.h>

int main() {
  volatile double x = 0.0;
  while(x < 100.0) {
	x += 0.2;
  }
  printf("%.20g\n", x);
}
