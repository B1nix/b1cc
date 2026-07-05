#include <stdio.h>

int test_vla(int n) {
    int arr[n];
    for (int i = 0; i < n; ++i) {
        arr[i] = i * 2;
    }
    int sum = 0;
    for (int i = 0; i < n; ++i) {
        sum += arr[i];
    }
    return sum;
}

int main() {
    int res = test_vla(10);
    if (res != 90) return 1;
    
    int res2 = test_vla(20);
    if (res2 != 380) return 2;

    printf("ok vla: res = %d, res2 = %d\n", res, res2);
    return 0;
}
