char *program_invocation_name = (char *)"b1nix";
char *tzname[1] = { (char *)"UTC" };

int main(void) {
    static char stdname[16] = "UTC";
    if (program_invocation_name == 0) return 1;
    if (tzname[0] == 0) return 2;
    if (stdname[0] != 'U') return 3;
    return 42;
}
