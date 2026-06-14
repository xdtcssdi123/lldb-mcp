#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bug: off-by-one in the loop - should be i <= n, not i < n.
   For n=5 this produces 96 instead of 120. */

int factorial(int n) {
    if (n < 0) return -1;
    int result = 1;
    for (int i = 2; i <= n; i++) {  /* FIXED: was i < n */
        result *= i;
    }
    return result;
}

int main() {
    int test_cases[] = {0, 1, 5, 6, 10};
    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);

    for (int i = 0; i < num_cases; i++) {
        int n = test_cases[i];
        int result = factorial(n);
        printf("factorial(%d) = %d\n", n, result);
    }

    return 0;
}
