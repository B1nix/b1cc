int main(void) {
    int i = 0;
    int sum = 0;
    do {
        i++;
        if (i == 2) continue;
        sum += i;
    } while (i < 4);

    switch (sum) {
    case 8:
        sum += 10;
        break;
    default:
        return 1;
    }

    if (sum != 18) return 2;
    goto done;
    return 3;
done:
    ;
    return 0;
}
