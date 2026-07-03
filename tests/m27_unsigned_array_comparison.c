unsigned int values[3] = {0, 0, 4294956041U};
unsigned short flag = 1;
unsigned int result = 1;

int main(void) {
    result = (flag &= (10U >= values[2]));
    if (flag != 0) {
        return 1;
    }
    if (result != 0) {
        return 2;
    }
    return 42;
}
