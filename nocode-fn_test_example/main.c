#include <stddef.h>
#include <stdio.h>
void merge_two_array(int* nums1, int size1, int* nums2, int size2, int *result) {
    int curr_idx_1 = 0;
    int curr_idx_2 = 0;
    int curr_idx_res = 0;
    while (curr_idx_1 < size1 && curr_idx_2 < size2) {
        if (nums1[curr_idx_1] < nums2[curr_idx_2]) {
            result[curr_idx_res] = nums1[curr_idx_1];
            curr_idx_1++;
        }else {
            result[curr_idx_res] = nums2[curr_idx_2];
            curr_idx_2++;
        }
        curr_idx_res++;
    }
    while (curr_idx_1 < size1) {
        result[curr_idx_res] = nums1[curr_idx_1];
        curr_idx_1++;
        curr_idx_res++;
    }
    while (curr_idx_2 < size2) {
        result[curr_idx_res] = nums2[curr_idx_2];
        curr_idx_2++;
        curr_idx_res++;
    }
}

int main() {
    int a[]={1,3,5,9,10};
    int b[] = {2,4,6,7,8};
    int c[sizeof(a)/sizeof(int) + sizeof(b)/sizeof(int)];
    merge_two_array(a, sizeof(a)/sizeof(int), b, sizeof(b)/sizeof(int), c);
    for (int i = 0; i < sizeof(c)/sizeof(int); i++) {
        printf("%d, ", c[i]);
    }
    printf("\n");
}