#include<stdio.h>
#include<stdlib.h>
#include<assert.h>

/* fpsanitizer: A micro benchmark to test performance overheads with
   frequent mallocs and frees */

float** matrix_mult(float** A, float** B,
		    int m1, int n1, int m2, int n2){
  
  assert (n1 == m2);
  float** result = malloc(m1 * sizeof(float *));

  for(int i =0 ; i < m1; i++){
    result[i] = malloc(n2 * sizeof(float));
    for(int j = 0; j < n2; j++){
      result[i][j] = 0.0f;
    }
  }

  for(int i =0; i < m1; i++){
    for(int j= 0; j< n2; j++){
      result[i][j] = 0;
      for(int k = 0; k < n1; k++){
	result[i][j] += A[i][k] * B[k][j];
      }
    }
  }
  return result;
}

float** allocate_matrix(int m1, int n1){

  float ** matrix;
  matrix = malloc(m1 * sizeof(float *));
  for(int i = 0; i< m1; i++){
    matrix[i] = malloc(n1 * sizeof(float));
  }  
  return matrix;
}
void print_free(float** result, int m1, int n1){

#ifdef PRINT_RESULT  
  for(int i = 0; i< m1; i++){
    for(int j = 0; j < n1; j++){
      printf("%f ", result[i][j]);
    }
    printf("\n");
  }
#endif


  for(int i = 0; i < m1; i++){
    free(result[i]);
  }
  free(result);
}

int main(int argc, char** argv){

  char* filename = "test.txt";
  int m1, n1, m2, n2;

  if(argc < 2){
    printf("insufficient arguments\n");
    return 1;
  }

  int iterations = atoi(argv[1]);

  FILE* fp = fopen(filename, "r");
  assert (fp != NULL);
  fscanf(fp, "%d %d %d %d\n", &m1, &n1, &m2, &n2);

  float** A = allocate_matrix(m1, n1);
  float** B = allocate_matrix(m2, n2);

  for(int i = 0; i < m1; i++){
    for(int j = 0; j < n1; j++){
      fscanf(fp, "%f", &A[i][j]);
    }
    fscanf(fp, "\n");
  }

  for(int i = 0; i < m2; i++){
    for(int j = 0; j < n2; j++){
      fscanf(fp, "%f", &B[i][j]);
    }
    fscanf(fp, "\n");
  }

  for(int i = 0; i < iterations; i++){
    float** result = matrix_mult(A,B, m1, n1, m2, n2);
    print_free(result, m1, n2);
  }  
 
  return 0;
}
