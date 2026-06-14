#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bug: off-by-one in loop bounds. For n=5 produces 24 instead of 120. */
int factorial(int n) {
    if (n < 0) return -1;
    int result = 1;
    for (int i = 2; i < n; i++) {  // BUG: should be i <= n
        result *= i;
    }
    return result;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <num> [num2 ...]\n", argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        int n = atoi(argv[i]);
        if (n < 0) {
            printf("factorial(%d) = error (negative)\n", n);
        } else {
            printf("factorial(%d) = %d\n", n, factorial(n));
        }
    }

    return 0;
}
