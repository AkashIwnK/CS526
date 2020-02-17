struct RT {
    int v;
    int u;
};
struct ST {
    int p;
    int q;
};
int main () {
    struct ST *st;
    struct RT *rt;
    *st = rt;
    return 0;
}
