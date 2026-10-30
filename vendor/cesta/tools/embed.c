#include <ctype.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 3 || !(argc % 2)) { fputs("usage: embed NAME FILE [NAME FILE ...]\n",stderr); return 2; }
    puts("#include \"cesta.h\"");
    for (int i = 1; i < argc; i += 2) {
        for (const unsigned char *p = (unsigned char *)argv[i]; *p; ++p)
            if (!(*p == '_' || isalpha(*p) || (p != (unsigned char *)argv[i] && isdigit(*p)))) return 2;
        FILE *file = fopen(argv[i+1],"rb");
        if (!file) { perror(argv[i+1]); return 2; }
        printf("static unsigned char data_%s[] = {\n",argv[i]);
        int c; size_t n = 0;
        while ((c = fgetc(file)) != EOF) {
            printf("%u,",(unsigned)c);
            if (!(++n % 32)) putchar('\n');
        }
        if (ferror(file) || fclose(file)) return 2;
        printf("0};\nCeText ce_builtin_%s(void) { return (CeText){data_%s,%zu}; }\n",argv[i],argv[i],n);
    }
    return ferror(stdout) ? 2 : 0;
}
