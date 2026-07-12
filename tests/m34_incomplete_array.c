extern int values[];
int values[3];
int main(void) {
    values[0] = 1;
    values[1] = 2;
    values[2] = 3;
    return values[0] + values[1] + values[2] == 6 ? 0 : 1;
}
