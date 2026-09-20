#include <stdio.h>

void to_base_n(int num, int base){
	const char digits[] = "0123456789abcdefg";
	int r = num % base;

	if (num >= base){
		to_base_n(num / base , base);

	}
	printf("%c", digits[r]);
}

void with_prefix(int num, int base){
	if (base < 2 || base > 16){
		printf("Error! base must be between 2 and 16");
		return;

	}

	if (base == 8) printf("0");
	if (base == 16) printf("0x");

	to_base_n(num, base);
}


int main(void) {
	printf("Testing Base 8 (21): ");
    with_prefix(21, 8); // 025
    printf("\n");

    printf("Testing Base 16 (129): ");
    with_prefix(129, 16); // 0x81
    printf("\n");

    printf("Testing Invalid Base (129, 20): ");
    with_prefix(129, 20); // Error message
    printf("\n");

    return 0;
}